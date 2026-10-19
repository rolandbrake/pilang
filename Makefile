CC = gcc
EMCC = emcc
RELEASE_DIR ?= bin

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
	src/pi_main.c \
	src/pi_token.c \
	src/pi_lex.c \
	src/pi_list.c \
	src/pi_stack.c \
	src/pi_table.c \
	src/pi_string.c \
	src/pi_value.c \
	src/pi_object.c \
	src/pi_class.c \
	src/pi_compiler.c \
	src/pi_parser.c \
	src/pi_vm.c \
	src/common.c \
	src/pi_func.c \
	src/pi_frame.c \
	src/gc.c \
	src/pi_package.c \
	src/pi_module.c \
	src/pi_build.c

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
	builtin/pi_loon.c \
	builtin/pi_obj.c \
	builtin/pi_methods.c \
	builtin/pi_builtin.c

IMAGE_SRCS := \
	builtin/image/pi_image.c \
	builtin/image/pi_filters.c \
	builtin/image/pi_color.c

AUDIO_SRCS := \
	builtin/audio/pi_audio.c

NATIVE_EXTRA_SRCS := \
	builtin/net/pi_socket.c \
	builtin/pi_os.c \
	builtin/pi_draw.c \
	builtin/pi_plot.c \
	builtin/pi_plot3d.c \
	$(IMAGE_SRCS) \
	$(AUDIO_SRCS)

NATIVE_SRCS := $(CORE_SRCS) $(COMMON_BUILTIN_SRCS) $(NATIVE_EXTRA_SRCS)
WEB_SRCS := $(CORE_SRCS) $(COMMON_BUILTIN_SRCS) \
	builtin/pi_draw.c builtin/pi_plot3d.c $(IMAGE_SRCS) $(AUDIO_SRCS)

CSTD ?= -std=c99
# WARNINGS ?= -Wall -Wextra

CPPFLAGS ?= -I.
DEBUG_CFLAGS ?=  -DDEBUG_BUILD
RELEASE_CFLAGS ?=  -O3

ifeq ($(OS),Windows_NT)
STATIC_PREFIX ?= third_party/prefix
NATIVE_CPPFLAGS = $(CPPFLAGS) -I$(STATIC_PREFIX)/include
RELEASE_CFLAGS += -static
DEBUG_CFLAGS += -static
NATIVE_LDLIBS ?= \
	-L$(STATIC_PREFIX)/lib \
	-lmingw32 \
	-lSDL2main \
	-lSDL2_image \
	-lSDL2_mixer \
	-lSDL2_ttf \
	-lSDL2 \
	-lfreetype \
	-lwinpthread \
	-lwinmm \
	-lole32 \
	-loleaut32 \
	-luuid \
	-lsetupapi \
	-limm32 \
	-lversion \
	-lshlwapi \
	-lws2_32 \
	-lopengl32 \
	-lkernel32 \
	-luser32 \
	-lgdi32 \
	-ladvapi32 \
	-lshell32
DEBUG_LDLIBS ?= $(NATIVE_LDLIBS)
else
PKG_CONFIG ?= pkg-config
SDL_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_image SDL2_ttf 2>/dev/null)
SDL_LDLIBS ?= $(shell $(PKG_CONFIG) --libs sdl2 SDL2_image SDL2_ttf 2>/dev/null)
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $(SDL_CFLAGS)
NATIVE_LDLIBS ?= $(SDL_LDLIBS) -lm -pthread
DEBUG_LDLIBS ?= $(NATIVE_LDLIBS)
NATIVE_CPPFLAGS = $(CPPFLAGS)
endif

CFLAGS = $(CPPFLAGS) $(WARNINGS) $(CSTD)
NATIVE_CFLAGS = $(NATIVE_CPPFLAGS) $(WARNINGS) $(CSTD)

EMCC_FLAGS ?= \
	-s ALLOW_MEMORY_GROWTH \
	-s STACK_SIZE=1MB \
	-s MODULARIZE=1 \
	-s EXPORT_NAME=createPilang \
	-s EXPORT_ES6=1 \
	-s EXPORTED_FUNCTIONS='["_main","_set_source","_execute_source","_execute_px","_disassemble_source","_stop_execution","_pause_execution","_resume_execution"]' \
	-s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","FS"]' \
	-Os \
	-s FULL_ES3=1 \
	-s OFFSCREEN_FRAMEBUFFER=1 \
	--use-port=sdl2 \
	--use-port=sdl2_image:formats=png \
	--use-port=sdl2_ttf \
	--shell-file index.html

.PHONY: all debug release static-deps web run test loon-test clean

all: release

$(RELEASE_DIR):
	$(MKDIR)

static-deps:
	powershell -NoProfile -ExecutionPolicy Bypass -File tools/setup_deps.ps1

debug: $(RELEASE_DIR)
	$(CC) $(NATIVE_CFLAGS) $(DEBUG_CFLAGS) -o $(TARGET) $(NATIVE_SRCS) $(DEBUG_LDLIBS)

release: $(RELEASE_DIR)
	$(CC) $(NATIVE_CFLAGS) $(RELEASE_CFLAGS) -o $(TARGET) $(NATIVE_SRCS) $(NATIVE_LDLIBS)

web: $(RELEASE_DIR)
	$(EMCC) $(CPPFLAGS) $(EMCC_FLAGS) -o $(WEB_TARGET) $(WEB_SRCS)

run: release
	./$(TARGET)

test: release
	python tools/run_tests.py

loon-test:
	$(MAKE) release
	./$(TARGET) test/loon/basic.pi

clean:
	$(CLEAN)
