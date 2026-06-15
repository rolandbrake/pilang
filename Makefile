CC = gcc
EMCC = emcc
RELEASE_DIR ?= release

ifeq ($(OS),Windows_NT)
EXEEXT := .exe
MKDIR = powershell -NoProfile -Command "New-Item -ItemType Directory -Force '$(RELEASE_DIR)' | Out-Null"
CLEAN = powershell -NoProfile -Command "Remove-Item -Force -ErrorAction SilentlyContinue '$(TARGET)', '$(WEB_TARGET)', '$(WEB_JS)', '$(WEB_WASM)'"
else
EXEEXT :=
MKDIR = mkdir -p $(RELEASE_DIR)
CLEAN = rm -f $(TARGET) $(WEB_TARGET) $(WEB_JS) $(WEB_WASM)
endif

TARGET ?= $(RELEASE_DIR)/pilang$(EXEEXT)
WEB_TARGET ?= $(RELEASE_DIR)/pilang.html
WEB_JS ?= $(RELEASE_DIR)/pilang.js
WEB_WASM ?= $(RELEASE_DIR)/pilang.wasm

CORE_SRCS := \
	pi_main.c \
	pi_token.c \
	pi_lex.c \
	pi_list.c \
	pi_stack.c \
	pi_table.c \
	pi_string.c \
	pi_value.c \
	pi_object.c \
	pi_compiler.c \
	pi_parser.c \
	pi_vm.c \
	common.c \
	pi_func.c \
	pi_frame.c \
	gc.c \
	pi_module.c

COMMON_BUILTIN_SRCS := \
	builtin/pi_math.c \
	builtin/pi_stats.c \
	builtin/pi_time.c \
	builtin/_pi_string.c \
	builtin/pi_io.c \
	builtin/pi_fs.c \
	builtin/pi_sys.c \
	builtin/pi_col.c \
	builtin/pi_func.c \
	builtin/pi_tensor.c \
	builtin/pi_type.c \
	builtin/pi_random.c \
	builtin/pi_lang.c \
	builtin/pi_obj.c \
	builtin/pi_methods.c \
	builtin/pi_builtin.c

IMAGE_SRCS := \
	builtin/image/pi_image.c \
	builtin/image/pi_filters.c \
	builtin/image/pi_color.c

NATIVE_EXTRA_SRCS := \
	builtin/pi_os.c \
	builtin/pi_draw.c \
	builtin/pi_plot.c \
	builtin/pi_plot3d.c \
	$(IMAGE_SRCS)

NATIVE_SRCS := $(CORE_SRCS) $(COMMON_BUILTIN_SRCS) $(NATIVE_EXTRA_SRCS)
WEB_SRCS := $(CORE_SRCS) $(COMMON_BUILTIN_SRCS)

CSTD ?= -std=c99
WARNINGS ?= -Wall -Wextra

DEBUG_CFLAGS ?= -g -DDEBUG_BUILD $(CSTD) -pthread
RELEASE_CFLAGS ?= -g -O3 $(CSTD) -static-libgcc -static-libstdc++

NATIVE_LDLIBS ?= \
	-lmingw32 \
	-lSDL2main \
	-lSDL2_image \
	-lSDL2_Mixer \
	-lSDL2_ttf \
	-lSDL2 \
	-Wl,-Bstatic \
	-lwinpthread \
	-Wl,-Bdynamic \
	-lwinmm \
	-lole32 \
	-loleaut32 \
	-luuid \
	-lsetupapi \
	-limm32 \
	-lversion \
	-lshlwapi

DEBUG_LDLIBS ?= \
	-lmingw32 \
	-lSDL2main \
	-lSDL2_image \
	-lSDL2_Mixer \
	-lSDL2_ttf \
	-lSDL2 \
	-lshlwapi

EMCC_FLAGS ?= \
	-s ASYNCIFY \
	-s ASYNCIFY_STACK_SIZE=524288 \
	-s ALLOW_MEMORY_GROWTH \
	-s MODULARIZE=1 \
	-s EXPORT_NAME=createMyModule \
	-s EXPORT_ES6=1 \
	-s EXPORTED_FUNCTIONS='["_main","_set_source","_execute_source","_disassemble_source","_stop_execution","_pause_execution","_resume_execution"]' \
	-s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
	-Os \
	-s FULL_ES3=1 \
	-s OFFSCREEN_FRAMEBUFFER=1 \
	--shell-file index.html

.PHONY: all debug release web run test clean

all: release

$(RELEASE_DIR):
	$(MKDIR)

debug: $(RELEASE_DIR)
	$(CC) $(DEBUG_CFLAGS) -o $(TARGET) $(NATIVE_SRCS) $(DEBUG_LDLIBS)

release: $(RELEASE_DIR)
	$(CC) $(RELEASE_CFLAGS) -o $(TARGET) $(NATIVE_SRCS) $(NATIVE_LDLIBS)

web: $(RELEASE_DIR)
	$(EMCC) $(EMCC_FLAGS) -o $(WEB_TARGET) $(WEB_SRCS)

run: release
	./$(TARGET)

test: release
	python tools/run_tests.py

clean:
	$(CLEAN)
