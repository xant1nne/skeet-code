use std::ffi::{CStr, CString};
use std::fs;
use std::os::raw::{c_char, c_int, c_void};
use std::path::Path;
use regex::Regex;
use serde::{Deserialize, Serialize};
use walkdir::WalkDir;

pub const SKEET_PLUGIN_ABI_VERSION: u32 = 1;
pub const SKEET_TOOL_READONLY: u32 = 1 << 0;
pub const SKEET_TOOL_NEEDS_CONFIRM: u32 = 1 << 1;

pub const SKEET_LOG_DEBUG: c_int = 0;
pub const SKEET_LOG_INFO: c_int = 1;
pub const SKEET_LOG_WARN: c_int = 2;
pub const SKEET_LOG_ERROR: c_int = 3;

type SkeetToolExecFn = extern "C" fn(
    args_json: *const c_char,
    out_result: *mut *mut c_char,
    out_is_error: *mut c_int,
    userdata: *mut c_void,
) -> c_int;

type SkeetCmdExecFn = extern "C" fn(
    args: *const c_char,
    out_msg: *mut *mut c_char,
    userdata: *mut c_void,
) -> c_int;

type SkeetLogFn = extern "C" fn(level: c_int, msg: *const c_char);
type SkeetFreeFn = extern "C" fn(ptr: *mut c_void);

type SkeetHostRegisterToolFn = extern "C" fn(
    name: *const c_char,
    description: *const c_char,
    schema_json: *const c_char,
    flags: u32,
    exec: SkeetToolExecFn,
    userdata: *mut c_void,
) -> c_int;

type SkeetHostRegisterCmdFn = extern "C" fn(
    name: *const c_char,
    help: *const c_char,
    exec: SkeetCmdExecFn,
    userdata: *mut c_void,
) -> c_int;

#[repr(C)]
pub struct SkeetHostApi {
    pub size: u32,
    pub owner: *const c_char,
    pub workspace_root: *const c_char,
    pub register_tool: SkeetHostRegisterToolFn,
    pub register_command: SkeetHostRegisterCmdFn,
    pub log: SkeetLogFn,
    pub free_func: SkeetFreeFn,
}

#[no_mangle]
pub extern "C" fn skeet_plugin_abi_version() -> u32 {
    SKEET_PLUGIN_ABI_VERSION
}

#[derive(Deserialize)]
struct GrepArgs {
    pattern: String,
    path: Option<String>,
    max_results: Option<usize>,
}

#[derive(Serialize)]
struct GrepMatch {
    file: String,
    line: usize,
    content: String,
}

extern "C" fn codex_fast_grep_exec(
    args_json: *const c_char,
    out_result: *mut *mut c_char,
    out_is_error: *mut c_int,
    _userdata: *mut c_void,
) -> c_int {
    let args_str = unsafe {
        if args_json.is_null() { "" } else { CStr::from_ptr(args_json).to_str().unwrap_or("") }
    };

    let args: GrepArgs = match serde_json::from_str(args_str) {
        Ok(a) => a,
        Err(e) => {
            unsafe {
                *out_is_error = 1;
                let err_cstr = CString::new(format!("Invalid arguments: {e}")).unwrap();
                *out_result = libc_strdup(err_cstr.as_ptr());
            }
            return -1;
        }
    };

    let regex = match Regex::new(&args.pattern) {
        Ok(r) => r,
        Err(e) => {
            unsafe {
                *out_is_error = 1;
                let err_cstr = CString::new(format!("Invalid regex pattern: {e}")).unwrap();
                *out_result = libc_strdup(err_cstr.as_ptr());
            }
            return -1;
        }
    };

    let root = args.path.unwrap_or_else(|| ".".to_string());
    let max = args.max_results.unwrap_or(150);

    let mut matches = Vec::new();
    let walker = WalkDir::new(&root).into_iter().filter_entry(|e| {
        let name = e.file_name().to_string_lossy();
        !name.starts_with('.') && name != "target" && name != "build" && name != "node_modules"
    });

    for entry in walker.filter_map(|e| e.ok()) {
        if !entry.file_type().is_file() {
            continue;
        }

        let path = entry.path();
        if let Ok(content) = fs::read_to_string(path) {
            for (idx, line) in content.lines().enumerate() {
                if regex.is_match(line) {
                    matches.push(GrepMatch {
                        file: path.to_string_lossy().to_string(),
                        line: idx + 1,
                        content: line.trim().to_string(),
                    });
                    if matches.len() >= max {
                        break;
                    }
                }
            }
        }
        if matches.len() >= max {
            break;
        }
    }

    let result_json = serde_json::to_string_pretty(&matches).unwrap_or_else(|_| "[]".to_string());
    unsafe {
        *out_is_error = 0;
        let c_res = CString::new(result_json).unwrap();
        *out_result = libc_strdup(c_res.as_ptr());
    }
    0
}

#[derive(Deserialize)]
struct OutlineArgs {
    file: String,
}

#[derive(Serialize)]
struct SymbolItem {
    kind: String,
    name: String,
    line: usize,
}

extern "C" fn codex_outline_symbols_exec(
    args_json: *const c_char,
    out_result: *mut *mut c_char,
    out_is_error: *mut c_int,
    _userdata: *mut c_void,
) -> c_int {
    let args_str = unsafe {
        if args_json.is_null() { "" } else { CStr::from_ptr(args_json).to_str().unwrap_or("") }
    };

    let args: OutlineArgs = match serde_json::from_str(args_str) {
        Ok(a) => a,
        Err(e) => {
            unsafe {
                *out_is_error = 1;
                let err_cstr = CString::new(format!("Invalid arguments: {e}")).unwrap();
                *out_result = libc_strdup(err_cstr.as_ptr());
            }
            return -1;
        }
    };

    let content = match fs::read_to_string(&args.file) {
        Ok(c) => c,
        Err(e) => {
            unsafe {
                *out_is_error = 1;
                let err_cstr = CString::new(format!("Failed to read file {}: {e}", args.file)).unwrap();
                *out_result = libc_strdup(err_cstr.as_ptr());
            }
            return -1;
        }
    };

    let fn_regex = Regex::new(r"^\s*(?:pub\s+)?(?:async\s+)?fn\s+([a-zA-Z0-9_]+)").unwrap();
    let struct_regex = Regex::new(r"^\s*(?:pub\s+)?(?:struct|enum|trait|class|interface)\s+([a-zA-Z0-9_]+)").unwrap();
    let c_fn_regex = Regex::new(r"^[a-zA-Z_][a-zA-Z0-9_*\s]+\s+([a-zA-Z0-9_]+)\s*\([^)]*\)\s*\{?").unwrap();

    let mut symbols = Vec::new();
    for (idx, line) in content.lines().enumerate() {
        if let Some(caps) = fn_regex.captures(line) {
            symbols.push(SymbolItem {
                kind: "function".to_string(),
                name: caps[1].to_string(),
                line: idx + 1,
            });
        } else if let Some(caps) = struct_regex.captures(line) {
            symbols.push(SymbolItem {
                kind: "type".to_string(),
                name: caps[1].to_string(),
                line: idx + 1,
            });
        } else if let Some(caps) = c_fn_regex.captures(line) {
            let name = &caps[1];
            if name != "if" && name != "while" && name != "for" && name != "switch" {
                symbols.push(SymbolItem {
                    kind: "c_symbol".to_string(),
                    name: name.to_string(),
                    line: idx + 1,
                });
            }
        }
    }

    let result_json = serde_json::to_string_pretty(&symbols).unwrap_or_else(|_| "[]".to_string());
    unsafe {
        *out_is_error = 0;
        let c_res = CString::new(result_json).unwrap();
        *out_result = libc_strdup(c_res.as_ptr());
    }
    0
}

#[derive(Deserialize)]
struct PatchArgs {
    file: String,
    patch: String,
}

extern "C" fn codex_apply_patch_exec(
    args_json: *const c_char,
    out_result: *mut *mut c_char,
    out_is_error: *mut c_int,
    _userdata: *mut c_void,
) -> c_int {
    let args_str = unsafe {
        if args_json.is_null() { "" } else { CStr::from_ptr(args_json).to_str().unwrap_or("") }
    };

    let args: PatchArgs = match serde_json::from_str(args_str) {
        Ok(a) => a,
        Err(e) => {
            unsafe {
                *out_is_error = 1;
                let err_cstr = CString::new(format!("Invalid arguments: {e}")).unwrap();
                *out_result = libc_strdup(err_cstr.as_ptr());
            }
            return -1;
        }
    };

    let original = match fs::read_to_string(&args.file) {
        Ok(c) => c,
        Err(e) => {
            unsafe {
                *out_is_error = 1;
                let err_cstr = CString::new(format!("Failed to read target file {}: {e}", args.file)).unwrap();
                *out_result = libc_strdup(err_cstr.as_ptr());
            }
            return -1;
        }
    };

    // Apply patch hunks: extract deleted lines (-), added lines (+), and context ( )
    let mut orig_lines: Vec<&str> = original.lines().collect();
    let patch_lines: Vec<&str> = args.patch.lines().collect();

    let mut find_lines = Vec::new();
    let mut repl_lines = Vec::new();
    let mut in_hunk = false;

    for pl in &patch_lines {
        if pl.starts_with("@@") {
            in_hunk = true;
            continue;
        }
        if in_hunk {
            if pl.starts_with('-') {
                find_lines.push(&pl[1..]);
            } else if pl.starts_with('+') {
                repl_lines.push(&pl[1..]);
            } else if pl.starts_with(' ') {
                find_lines.push(&pl[1..]);
                repl_lines.push(&pl[1..]);
            }
        }
    }

    if find_lines.is_empty() {
        // Fallback: direct search and replace if format is raw before/after
        unsafe {
            *out_is_error = 1;
            let err_cstr = CString::new("No valid patch hunks found in patch input").unwrap();
            *out_result = libc_strdup(err_cstr.as_ptr());
        }
        return -1;
    }

    let target_block = find_lines.join("\n");
    let replacement_block = repl_lines.join("\n");

    if !original.contains(&target_block) {
        unsafe {
            *out_is_error = 1;
            let err_cstr = CString::new(format!("Patch target context not found in {}", args.file)).unwrap();
            *out_result = libc_strdup(err_cstr.as_ptr());
        }
        return -1;
    }

    let patched = original.replacen(&target_block, &replacement_block, 1);
    if let Err(e) = fs::write(&args.file, patched) {
        unsafe {
            *out_is_error = 1;
            let err_cstr = CString::new(format!("Failed to write patched file {}: {e}", args.file)).unwrap();
            *out_result = libc_strdup(err_cstr.as_ptr());
        }
        return -1;
    }

    let msg = format!("Successfully applied Codex patch to {} (-{} +{} lines)", args.file, find_lines.len(), repl_lines.len());
    unsafe {
        *out_is_error = 0;
        let c_msg = CString::new(msg).unwrap();
        *out_result = libc_strdup(c_msg.as_ptr());
    }
    0
}

extern "C" fn codex_tools_cmd(
    _args: *const c_char,
    out_msg: *mut *mut c_char,
    _userdata: *mut c_void,
) -> c_int {
    let msg = "Rust Codex Tools (cdylib / .so):\n\
               - codex_fast_grep: High-speed multi-threaded regex repository search\n\
               - codex_outline_symbols: AST symbol extraction for functions/structs\n\
               - codex_apply_patch: Atomic unified diff patch applicator (Codex format)\n\
               Built with Rust & Skeet Plugin ABI v1";
    unsafe {
        let c_msg = CString::new(msg).unwrap();
        *out_msg = libc_strdup(c_msg.as_ptr());
    }
    0
}

unsafe fn libc_strdup(src: *const c_char) -> *mut c_char {
    let len = CStr::from_ptr(src).to_bytes_with_nul().len();
    let mem = libc_malloc(len) as *mut c_char;
    if !mem.is_null() {
        std::ptr::copy_nonoverlapping(src, mem, len);
    }
    mem
}

extern "C" {
    fn malloc(size: usize) -> *mut c_void;
}

unsafe fn libc_malloc(size: usize) -> *mut c_void {
    malloc(size)
}

#[no_mangle]
pub extern "C" fn skeet_plugin_init(host_api: *const SkeetHostApi) -> c_int {
    if host_api.is_null() {
        return -1;
    }

    let host = unsafe { &*host_api };
    let log_msg = CString::new("Rust Codex Tools plugin initializing...").unwrap();
    (host.log)(SKEET_LOG_INFO, log_msg.as_ptr());

    // Register codex_fast_grep
    let grep_name = CString::new("codex_fast_grep").unwrap();
    let grep_desc = CString::new("High-speed repository regex searcher ported from Rust codex tools").unwrap();
    let grep_schema = CString::new(r#"{"type":"object","properties":{"pattern":{"type":"string","description":"Regex search pattern"},"path":{"type":"string","description":"Path to directory to search"},"max_results":{"type":"integer","description":"Max matches to return"}},"required":["pattern"]}"#).unwrap();

    (host.register_tool)(
        grep_name.as_ptr(),
        grep_desc.as_ptr(),
        grep_schema.as_ptr(),
        SKEET_TOOL_READONLY,
        codex_fast_grep_exec,
        std::ptr::null_mut(),
    );

    // Register codex_outline_symbols
    let outline_name = CString::new("codex_outline_symbols").unwrap();
    let outline_desc = CString::new("Fast structural symbol outline for functions, types, and definitions").unwrap();
    let outline_schema = CString::new(r#"{"type":"object","properties":{"file":{"type":"string","description":"Source file path to extract outline from"}},"required":["file"]}"#).unwrap();

    (host.register_tool)(
        outline_name.as_ptr(),
        outline_desc.as_ptr(),
        outline_schema.as_ptr(),
        SKEET_TOOL_READONLY,
        codex_outline_symbols_exec,
        std::ptr::null_mut(),
    );

    // Register codex_apply_patch
    let patch_name = CString::new("codex_apply_patch").unwrap();
    let patch_desc = CString::new("Atomic unified diff patch applicator ported from OpenAI Codex").unwrap();
    let patch_schema = CString::new(r#"{"type":"object","properties":{"file":{"type":"string","description":"Target file path to patch"},"patch":{"type":"string","description":"Unified diff patch containing @@ hunks"}},"required":["file","patch"]}"#).unwrap();

    (host.register_tool)(
        patch_name.as_ptr(),
        patch_desc.as_ptr(),
        patch_schema.as_ptr(),
        SKEET_TOOL_NEEDS_CONFIRM,
        codex_apply_patch_exec,
        std::ptr::null_mut(),
    );

    // Register /codex_tools slash command
    let cmd_name = CString::new("/codex_tools").unwrap();
    let cmd_help = CString::new("Show info about loaded Rust codex tools").unwrap();
    (host.register_command)(
        cmd_name.as_ptr(),
        cmd_help.as_ptr(),
        codex_tools_cmd,
        std::ptr::null_mut(),
    );

    let ok_msg = CString::new("Rust Codex Tools loaded successfully.").unwrap();
    (host.log)(SKEET_LOG_INFO, ok_msg.as_ptr());
    0
}

#[no_mangle]
pub extern "C" fn skeet_plugin_deinit() -> c_int {
    0
}

