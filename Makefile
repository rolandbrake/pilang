CC = gcc
EMCC = emcc
RELEASE_DIR ?= bin

ifeq ($(OS),Windows_NT)
EXEEXT := .exe
ifeq ($(MSYSTEM),)
USE_POWERSHELL := 1
endif
endif

ifeq ($(USE_POWERSHELL),1)
MKDIR = powershell -NoProfile -Command "New-Item -ItemType Directory -Force '$(RELEASE_DIR)' | Out-Null"
CLEAN = powershell -NoProfile -Command "Remove-Item -Force -ErrorAction SilentlyContinue '$(TARGET)', '$(WEB_TARGET)', '$(WEB_JS)', '$(WEB_WASM)'"
else
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
# Windows uses the checked-in static build kit. `make release` therefore
# produces a self-contained executable with no SDL DLLs.
STATIC_PREFIX ?= deps/windows-x64
ifeq ($(wildcard $(STATIC_PREFIX)/include/SDL2/SDL.h),)
$(error Missing Windows static build kit at $(STATIC_PREFIX). Clone the repository with the deps directory.)
endif
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
else ifeq ($(shell uname -s),Linux)
# Linux uses its own checked-in static kit. The pkg-config metadata carries
# every static SDL dependency, so no shared libraries are needed in a release.
STATIC_PREFIX ?= deps/linux-x64
ifeq ($(wildcard $(STATIC_PREFIX)/include/SDL2/SDL.h),)
$(error Missing Linux static build kit at $(STATIC_PREFIX).)
endif
NATIVE_CPPFLAGS = $(CPPFLAGS) -I$(STATIC_PREFIX)/include $(shell PKG_CONFIG_PATH=$(STATIC_PREFIX)/lib/pkgconfig pkg-config --cflags sdl2 SDL2_image SDL2_mixer SDL2_ttf)
RELEASE_CFLAGS += -static
DEBUG_CFLAGS += -static
NATIVE_LDLIBS ?= $(shell PKG_CONFIG_PATH=$(STATIC_PREFIX)/lib/pkgconfig pkg-config --static --libs sdl2 SDL2_image SDL2_mixer SDL2_ttf) -lm -ldl -lpthread
else
$(error Native builds are currently supported on Windows x64 and Linux x64 only.)
endif
DEBUG_LDLIBS ?= $(NATIVE_LDLIBS)

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

.PHONY: all debug release web run test loon-test clean

all: release

$(RELEASE_DIR):
	$(MKDIR)

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
