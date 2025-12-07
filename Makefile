CC = clang
CFLAGS = -Wall -Wextra -pedantic -std=c99 -g
LDFLAGS =

# Lua configuration (adjust paths for your system)
# macOS Homebrew defaults
LUA_INC ?= /opt/homebrew/include/lua
LUA_LIB ?= /opt/homebrew/lib
LUA_VERSION ?= 5.4

# For Linux, you might use:
# LUA_INC ?= /usr/include/lua5.4
# LUA_LIB ?= /usr/lib

SRC_DIR = src
BUILD_DIR = build
BIN = luap
LIB = luapp.so

# Core sources (exclude interop for standalone binary)
CORE_SRCS = $(filter-out $(SRC_DIR)/luapp_interop.c, $(wildcard $(SRC_DIR)/*.c))
CORE_OBJS = $(CORE_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# All sources including interop
ALL_SRCS = $(wildcard $(SRC_DIR)/*.c)
LIB_SRCS = $(filter-out $(SRC_DIR)/main.c, $(ALL_SRCS))
LIB_OBJS = $(LIB_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.pic.o)

.PHONY: all clean run lib install-lib

all: $(BIN)

# Standalone interpreter
$(BIN): $(CORE_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# Shared library for Lua interop
lib: $(LIB)

# macOS uses .dylib, Linux uses .so
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    SHARED_FLAGS = -dynamiclib -undefined dynamic_lookup
    LIB = luapp.so
else
    SHARED_FLAGS = -shared
    LIB = luapp.so
endif

$(LIB): $(LIB_OBJS)
	$(CC) $(SHARED_FLAGS) -o $@ $^ -L$(LUA_LIB) -llua

# Regular object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Position-independent object files for shared library
$(BUILD_DIR)/%.pic.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fPIC -I$(LUA_INC) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(BIN) $(LIB)

run: $(BIN)
	./$(BIN)

# Install shared library to Lua's package.cpath
install-lib: $(LIB)
	@echo "Install $(LIB) to your Lua cpath, e.g.:"
	@echo "  cp $(LIB) /usr/local/lib/lua/$(LUA_VERSION)/"
