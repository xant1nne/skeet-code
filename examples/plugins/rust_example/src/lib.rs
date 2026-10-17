// Minimal Rust example plugin for skeet
// Build with: ./build.sh (Linux)

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_uint};

#[repr(C)]
pub struct SkeetHostApi {
    pub register_tool: Option<extern "C" fn(*const c_char) -> c_int>,
    pub register_command: Option<extern "C" fn(*const c_char) -> c_int>,
    pub log: Option<extern "C" fn(c_int, *const c_char)>,
    pub call_host_tool: Option<extern "C" fn(*const c_char, *const c_char, *mut *mut c_char) -> c_int>,
    pub free_func: Option<extern "C" fn(*mut std::ffi::c_void)>,
    pub owner: *const c_char,
}

unsafe fn cstr_to_string(ptr: *const c_char) -> String {
    if ptr.is_null() { return String::new(); }
    CStr::from_ptr(ptr).to_string_lossy().into_owned()
}

#[no_mangle]
pub extern "C" fn skeet_plugin_abi_version() -> c_uint {
    1
}

#[no_mangle]
pub extern "C" fn skeet_plugin_init(api: *const SkeetHostApi) -> c_int {
    if api.is_null() { return -1; }
    let api = unsafe { &*api };

    // Register a slash command: /hello
    let cmd_json = r#"{ "name": "/hello", "description": "Say hello from Rust plugin" }"#;
    let c_cmd = CString::new(cmd_json).unwrap();
    if let Some(register_command) = api.register_command {
        let rc = register_command(c_cmd.as_ptr());
        if rc != 0 {
            if let Some(log) = api.log {
                let m = CString::new("rust_plugin: register_command failed").unwrap();
                log(2, m.as_ptr());
            }
            return -2;
        }
    }

    // Register a tool: format_code (example schema simplified)
    let tool_json = r#"{
        "name": "format_code",
        "schema": { "type": "object", "properties": { "text": { "type": "string" } }, "required": ["text"] },
        "description": "Format C code using a simple style"
    }"#;
    let c_tool = CString::new(tool_json).unwrap();
    if let Some(register_tool) = api.register_tool {
        let rc = register_tool(c_tool.as_ptr());
        if rc != 0 {
            if let Some(log) = api.log {
                let m = CString::new("rust_plugin: register_tool failed").unwrap();
                log(2, m.as_ptr());
            }
            // not fatal
        }
    }

    // Log owner info
    if let Some(log) = api.log {
        let owner = unsafe { cstr_to_string(api.owner) };
        let msg = format!("rust_plugin: initialized (owner={})", owner);
        let c_msg = CString::new(msg).unwrap();
        log(1, c_msg.as_ptr());
    }

    0
}

#[no_mangle]
pub extern "C" fn skeet_plugin_deinit() -> c_int {
    // nothing to cleanup in this simple example
    0
}
