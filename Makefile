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

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS) $(MAIN_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET) test_* mock_server

TESTS = test_arena test_str test_sse test_json test_events test_commands test_permissions test_registry test_accumulator test_backend_llama test_tools test_hw test_tune

test: $(TESTS)
	@for t in $(TESTS); do \
		echo "===> Running $$t"; \
		./$$t || exit 1; \
	done
	@echo "All tests passed successfully!"

CORE_TEST_OBJS = src/arena.c src/str.c src/log.c src/compat.c src/context.c src/events.c src/commands.c src/permissions.c src/tune/hw.c src/tune/gguf.c src/tune/tune.c src/tools/registry.c src/tools/accumulator.c src/tools/diff.c src/tools/text_fallback.c src/tools/read_file.c src/tools/glob.c src/tools/grep.c src/tools/edit_file.c src/tools/write_file.c src/tools/bash.c src/tools/builtins.c src/json.c src/backend_ollama.c src/backend_llama.c src/http.c third_party/cJSON.c

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

mock_server: tests/mock_server.c
	$(CC) $(CFLAGS) -o $@ $<
