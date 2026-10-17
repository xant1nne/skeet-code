CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -pedantic -g -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
SAN_FLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer

INCLUDES = -Isrc -Ithird_party
LIBS = $(shell pkg-config --libs libcurl 2>/dev/null || echo "-lcurl") -lm -ldl

SRC = src/arena.c \
      src/str.c \
      src/log.c \
      src/config.c \
      src/compat.c \
      src/context.c \
      src/events.c \
      src/commands.c \
      src/permissions.c \
      src/http.c \
      src/json.c \
      src/backend_ollama.c \
      src/backend_llama.c \
      src/agent.c \
      src/tui/term.c \
      src/tui/view.c \
      src/tui/input.c \
      src/tui/tui.c \
      src/plugins/plugin_loader.c \
      src/plugins/lua_host.c \
      src/tune/hw.c \
      src/tune/gguf.c \
      src/tune/tune.c \
      src/tools/registry.c \
      src/tools/accumulator.c \
      src/tools/diff.c \
      src/tools/text_fallback.c \
      src/tools/read_file.c \
      src/tools/glob.c \
      src/tools/grep.c \
      src/tools/edit_file.c \
      src/tools/write_file.c \
      src/tools/bash.c \
      src/tools/builtins.c \
      third_party/cJSON.c

OBJ_DIR = build
OBJS = $(patsubst %.c, $(OBJ_DIR)/%.o, $(SRC))
MAIN_OBJ = $(OBJ_DIR)/src/main.o

TARGET = skeet

# Build mode: DEBUG by default with ASan, RELEASE=1 for optimized build without sanitizers
ifeq ($(RELEASE), 1)
    CFLAGS += -O3 -DNDEBUG
else
    CFLAGS += $(SAN_FLAGS) -O1 -DDEBUG
endif

# ── Release configuration ────────────────────────────────────────────────────
PREFIX       ?= /usr/local
MANDIR       ?= $(PREFIX)/share/man/man1
SKEET_VERSION ?= 1.0.0

# Detect host OS for platform-specific release steps
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)

.PHONY: all clean test plugins release install uninstall

all: $(TARGET)

$(TARGET): $(OBJS) $(MAIN_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

plugins:
	@mkdir -p plugins
	$(CC) -shared -fPIC $(INCLUDES) plugins/sample_tool_plugin.c -o plugins/sample_plugin.so
	@if command -v cargo >/dev/null 2>&1; then \
		echo "Building Rust codex tools plugin..."; \
		(cd plugins/rust_codex_tools && cargo build --release) && \
		cp plugins/rust_codex_tools/target/release/libskeet_rust_codex_tools.so plugins/ 2>/dev/null || true; \
	fi

clean:
	rm -rf $(OBJ_DIR) $(TARGET) test_* mock_server plugins/*.so plugins/*.dll \
	       skeet-linux-* skeet-macos-* skeet-arm64 skeet-x86_64 dist/

# ── Release targets ──────────────────────────────────────────────────────────
# Linux: static binary via musl-gcc (installs musl-tools on Debian/Ubuntu)
# macOS: two-arch build + lipo universal
# Windows: strip is enough (no lipo)
release:
ifeq ($(UNAME_S), Linux)
	@echo "==> Building static Linux release with musl-gcc..."
	$(MAKE) clean
	CC=musl-gcc RELEASE=1 \
	    CFLAGS="-Wall -Wextra -std=c11 -pedantic -O3 -DNDEBUG -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE" \
	    $(MAKE) all
	strip $(TARGET)
	mv $(TARGET) skeet-linux-x86_64
	@echo "==> skeet-linux-x86_64 ready"
else ifeq ($(UNAME_S), Darwin)
	@echo "==> Building macOS universal release (arm64 + x86_64)..."
	$(MAKE) clean
	RELEASE=1 \
	    CFLAGS="-Wall -Wextra -std=c11 -pedantic -O3 -DNDEBUG -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -arch arm64" \
	    $(MAKE) all
	cp $(TARGET) skeet-arm64
	$(MAKE) clean
	RELEASE=1 \
	    CFLAGS="-Wall -Wextra -std=c11 -pedantic -O3 -DNDEBUG -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -arch x86_64" \
	    $(MAKE) all
	cp $(TARGET) skeet-x86_64
	lipo -create -output skeet-macos-universal skeet-arm64 skeet-x86_64
	strip skeet-macos-universal
	rm -f skeet-arm64 skeet-x86_64 $(TARGET)
	@echo "==> skeet-macos-universal ready"
else
	@echo "==> Building Windows release..."
	$(MAKE) clean
	RELEASE=1 $(MAKE) all
	strip $(TARGET).exe 2>/dev/null || strip $(TARGET)
	@echo "==> $(TARGET) ready"
endif

# ── Install / Uninstall ──────────────────────────────────────────────────────
install: all
	@echo "==> Installing skeet to $(PREFIX)/bin/"
	install -d $(PREFIX)/bin
	install -m 755 $(TARGET) $(PREFIX)/bin/$(TARGET)
	@if [ -f docs/skeet.1 ]; then \
	    echo "==> Installing man page to $(MANDIR)/"; \
	    install -d $(MANDIR); \
	    install -m 644 docs/skeet.1 $(MANDIR)/skeet.1; \
	    gzip -f $(MANDIR)/skeet.1 2>/dev/null || true; \
	fi
	@echo "==> Done. Run 'skeet --help' to get started."

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)
	rm -f $(MANDIR)/skeet.1 $(MANDIR)/skeet.1.gz
	@echo "==> Uninstalled."

TESTS = test_arena test_str test_sse test_json test_events test_commands test_permissions test_registry test_accumulator test_backend_llama test_tools test_hw test_tune test_tui test_plugins test_context_m6 test_lua_host

test: $(TESTS)
	@for t in $(TESTS); do \
		echo "===> Running $$t"; \
		./$$t || exit 1; \
	done
	@echo "All tests passed successfully!"

CORE_TEST_OBJS = src/arena.c src/str.c src/log.c src/compat.c src/config.c src/context.c src/events.c src/commands.c src/permissions.c src/tune/hw.c src/tune/gguf.c src/tune/tune.c src/tools/registry.c src/tools/accumulator.c src/tools/diff.c src/tools/text_fallback.c src/tools/read_file.c src/tools/glob.c src/tools/grep.c src/tools/edit_file.c src/tools/write_file.c src/tools/bash.c src/tools/builtins.c src/json.c src/backend_ollama.c src/backend_llama.c src/http.c src/tui/term.c src/tui/view.c src/tui/input.c src/tui/tui.c src/plugins/plugin_loader.c src/plugins/lua_host.c third_party/cJSON.c

test_arena: tests/test_arena.c src/arena.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

test_str: tests/test_str.c src/str.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

test_sse: tests/test_sse.c src/http.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

test_json: tests/test_json.c src/json.c src/str.c third_party/cJSON.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

test_events: tests/test_events.c $(CORE_TEST_OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

test_commands: tests/test_commands.c $(CORE_TEST_OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

test_permissions: tests/test_permissions.c $(CORE_TEST_OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

test_registry: tests/test_registry.c $(CORE_TEST_OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

test_accumulator: tests/test_accumulator.c src/tools/accumulator.c src/arena.c src/str.c src/json.c third_party/cJSON.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

test_backend_llama: tests/test_backend_llama.c $(CORE_TEST_OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

test_tools: tests/test_tools.c $(CORE_TEST_OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

test_hw: tests/test_hw.c src/tune/hw.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

test_tune: tests/test_tune.c src/tune/hw.c src/tune/gguf.c src/tune/tune.c third_party/cJSON.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

test_tui: tests/test_tui.c src/tui/term.c src/tui/view.c src/tui/input.c src/compat.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

test_plugins: tests/test_plugins.c $(CORE_TEST_OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

test_context_m6: tests/test_context_m6.c $(CORE_TEST_OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

test_lua_host: tests/test_lua_host.c $(CORE_TEST_OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

mock_server: tests/mock_server.c
	$(CC) $(CFLAGS) -o $@ $<
