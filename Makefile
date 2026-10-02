# tui-color-thief.wasm
#
# Makefile to use with emscripten
# See https://emscripten.org/docs/getting_started/downloads.html
# for installation instructions.
#
# This Makefile assumes you have loaded emscripten's environment.
# (On Windows, you may need to execute emsdk_env.bat or encmdprompt.bat ahead)
#
# Build(Debug)
# > emmake make BUILD_TYPE=Debug
#
# Build(Release)
# > emmake make BUILD_TYPE=Release
#
# Build(Release) with a Brotli-compressed wasm artifact
# > emmake make BUILD_TYPE=Release BROTLI=1
# Optional: BROTLI_QUALITY=0..11 (default: 11), BROTLI_CMD=/path/to/brotli
#
# Build-Output:object:
# - build/debug|release
# Build-Output:target:
# - ./web/debug
# - ./web/release
#

# Auto-sets parallel jobs to CPU core count
ifeq ($(OS),Windows_NT)
	MAKEFLAGS += -j$(if $(NUMBER_OF_PROCESSORS),$(NUMBER_OF_PROCESSORS),1)
else
	MAKEFLAGS += -j$(shell nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 1)
endif

################################################################
# Portable shell helpers (cmd.exe vs POSIX)

ifeq ($(OS),Windows_NT)
	SHELL := cmd
	.SHELLFLAGS := /C
	npath = $(subst /,\\,$(1))
define MKDIR
	if not exist "$(call npath,$(1))" mkdir "$(call npath,$(1))"
endef
	RM_DIR = rmdir /S /Q
else
	npath = $(1)
define MKDIR
	mkdir -p $(1)
endef
	RM_DIR = rm -rf
endif

################################################################

CC = emcc
CXX = em++

APP = tui-color-thief-uasm.js

BROTLI ?= 0
BROTLI_CMD ?= brotli
BROTLI_QUALITY ?= 11

ifneq ($(filter $(BROTLI),0 1),$(BROTLI))
$(error BROTLI must be 0 or 1)
endif

# Debug/Release
# Note: allow utility targets like 'clean' to run without requiring BUILD_TYPE
# Fallback to a dummy value so variables that reference BUILD_TYPE_OUTPUT don't break on 'make clean'
ifneq (clean,$(firstword $(MAKECMDGOALS)))
	BUILD_TYPE_OUTPUT := $(BUILD_TYPE:Debug=debug)
	BUILD_TYPE_OUTPUT := $(BUILD_TYPE_OUTPUT:Release=release)
	ifneq ($(BUILD_TYPE_OUTPUT), debug)
		ifneq ($(BUILD_TYPE_OUTPUT), release)
			$(error BUILD_TYPE variable is not set)
		endif
	endif
else
	BUILD_TYPE_OUTPUT := debug
endif

ROOT = .
OUTPUT = $(ROOT)/build/$(BUILD_TYPE_OUTPUT)
TARGET = $(ROOT)/web/$(BUILD_TYPE_OUTPUT)/$(APP)
WASM_TARGET = $(TARGET:.js=.wasm)
BROTLI_TARGET = $(WASM_TARGET).br

# Emscripten replaces the platform-specific N-API/JNI/Swift bindings with
# wasm_binding.cc.
CXX_SOURCES := src/tui-color-thief/wasm_binding.cc src/tui-color-thief/color_thief.cc
OBJECTS := $(patsubst %.cc,$(OUTPUT)/%.o,$(CXX_SOURCES))
DEPENDENCIES := $(OBJECTS:.o=.d)

COMMON_FLAGS += -MMD -MP -Isrc/tui-color-thief

CXX_FLAGS += $(COMMON_FLAGS) -std=c++20

LD_FLAGS += --no-entry
LD_FLAGS += -lembind
LD_FLAGS += -s WASM=1
LD_FLAGS += -s ALLOW_MEMORY_GROWTH=1
LD_FLAGS += -s EXPORTED_FUNCTIONS='["_malloc","_free"]'
LD_FLAGS += -s DYNAMIC_EXECUTION=0

ifeq ($(BUILD_TYPE),Debug)
	LD_FLAGS += -s ASSERTIONS=1
	LD_FLAGS += -s SAFE_HEAP=1
endif

# ("EM_FLAGS" options gets added to compile and link flags.)
ifeq ($(BUILD_TYPE),Debug)
	EM_FLAGS += -O0 -g
else ifeq ($(BUILD_TYPE),Release)
	EM_FLAGS += -O3
else
endif

# Added "EM_FLAGS" to both compile and link flags
CXX_FLAGS += $(EM_FLAGS)
LD_FLAGS += $(EM_FLAGS)

JS_FLAGS += -s MODULARIZE=1
JS_FLAGS += -s EXPORT_ES6=1
JS_FLAGS += -s ENVIRONMENT=web
JS_FLAGS += -s EXPORT_NAME=createTestUasmModule

.PHONY: build clean
build: $(TARGET) $(if $(filter 1,$(BROTLI)),$(BROTLI_TARGET))
	@echo Build complete for $(TARGET)
	$(if $(filter 1,$(BROTLI)),@echo Brotli artifact: $(BROTLI_TARGET))

$(TARGET): $(OBJECTS)
	@$(call MKDIR,$(@D))
	$(CXX) -o $@ $(OBJECTS) $(LD_FLAGS) $(JS_FLAGS)

$(BROTLI_TARGET): $(TARGET)
	$(BROTLI_CMD) -q $(BROTLI_QUALITY) -f -o $@ $(WASM_TARGET)

$(OUTPUT)/%.o: %.cc Makefile
	@$(call MKDIR,$(@D))
	$(CXX) $(CXX_FLAGS) -c $< -o $@

clean:
	-$(RM_DIR) "$(call npath,$(ROOT)/build)"
	-$(RM_DIR) "$(call npath,$(ROOT)/web/debug)"
	-$(RM_DIR) "$(call npath,$(ROOT)/web/release)"

-include $(DEPENDENCIES)
