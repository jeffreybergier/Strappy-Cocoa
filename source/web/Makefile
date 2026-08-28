# Browser-only WebAssembly build. Run this Makefile through the wasm-build
# Compose service so Emscripten never needs to be installed on the host.

EMCC := emcc
NODE := $(shell command -v node 2>/dev/null)
SHARED_DIR := ../shared
RESOURCE_DIR := $(SHARED_DIR)/Resources
BUILD_DIR := build-release
RESOURCE_OUTPUT_DIR := $(BUILD_DIR)/Resources
MODULE_NAME := strappy

WEB_SOURCES := \
	$(SHARED_DIR)/strappy_core.c \
	$(SHARED_DIR)/strappy_provider.c \
	$(SHARED_DIR)/strappy_client.c \
	$(SHARED_DIR)/strappy_client_web.c \
	strappy_sse_web.c \
	strappy_web_client.c

WEB_HEADERS := \
	$(SHARED_DIR)/strappy_core.h \
	$(SHARED_DIR)/strappy_provider.h \
	$(SHARED_DIR)/strappy_config.h \
	$(SHARED_DIR)/strappy_client.h \
	$(SHARED_DIR)/strappy_client_transport.h \
	$(SHARED_DIR)/strappy_sse.h

WEB_BUILD_INPUTS := Makefile strappy_client_fetch.js $(WEB_SOURCES) $(WEB_HEADERS)

WEB_JAVASCRIPT := $(BUILD_DIR)/$(MODULE_NAME).js
WEB_WASM := $(BUILD_DIR)/$(MODULE_NAME).wasm
WEB_OUTPUTS := $(WEB_JAVASCRIPT) $(WEB_WASM)
STATIC_INPUTS := index.html app.js worker.js
STATIC_OUTPUTS := $(addprefix $(BUILD_DIR)/,$(STATIC_INPUTS))
WORLD_KNOWLEDGE_RESOURCE_NAMES := \
	AssistantSets.json \
	BundledModels.json \
	FontAwesomeIcons.json \
	GuidanceSkills.json \
	GuidanceTools.json \
	SystemPrompt.json
WORLD_KNOWLEDGE_RESOURCE_INPUTS := \
	$(addprefix $(RESOURCE_DIR)/,$(WORLD_KNOWLEDGE_RESOURCE_NAMES))
WORLD_KNOWLEDGE_RESOURCE_OUTPUTS := \
	$(addprefix $(RESOURCE_OUTPUT_DIR)/,$(WORLD_KNOWLEDGE_RESOURCE_NAMES))
TEST_SCRIPT := browser_smoke_test.mjs
CLIENT_TEST_SCRIPT := client_transport_smoke_test.mjs

CPPFLAGS := -I$(SHARED_DIR)
COMMON_CFLAGS := \
	-std=c99 \
	-Wall \
	-Wextra \
	-Wconversion \
	-Wsign-conversion \
	-Wfloat-conversion
RELEASE_CFLAGS := -O3
EMSCRIPTEN_FLAGS := \
	--no-entry \
	-sWASM=1 \
	-sFILESYSTEM=0 \
	-sALLOW_MEMORY_GROWTH=1 \
	-sASYNCIFY=1 \
	-sMODULARIZE=1 \
	-sEXPORT_ES6=1 \
	-sEXPORT_NAME=createStrappyModule \
	-sENVIRONMENT=web,worker,node \
	-sEXPORTED_FUNCTIONS='["_strappy_utf8_validate","_strappy_web_client_test_request","_strappy_web_client_result_http_status","_strappy_web_client_result_model","_strappy_web_client_result_status","_strappy_web_client_result_output_text","_strappy_web_client_result_error","_strappy_client_web_cancel_active_request"]' \
	-sEXPORTED_RUNTIME_METHODS='["ccall"]'

.DEFAULT_GOAL := release

.PHONY: clean release test

release: $(WEB_OUTPUTS) $(STATIC_OUTPUTS) $(WORLD_KNOWLEDGE_RESOURCE_OUTPUTS)

test: release
	$(NODE) $(CLIENT_TEST_SCRIPT)
	$(NODE) $(TEST_SCRIPT)

# Emscripten creates the JavaScript loader and Wasm binary in one invocation.
$(WEB_OUTPUTS) &: $(WEB_BUILD_INPUTS) | $(BUILD_DIR)
	$(EMCC) $(CPPFLAGS) $(COMMON_CFLAGS) $(RELEASE_CFLAGS) \
		$(EMSCRIPTEN_FLAGS) --js-library strappy_client_fetch.js \
		$(WEB_SOURCES) -o $(WEB_JAVASCRIPT)

$(BUILD_DIR)/%: % | $(BUILD_DIR)
	cp $< $@

$(RESOURCE_OUTPUT_DIR)/%.json: $(RESOURCE_DIR)/%.json | $(RESOURCE_OUTPUT_DIR)
	cp $< $@

$(BUILD_DIR):
	mkdir -p $@

$(RESOURCE_OUTPUT_DIR): | $(BUILD_DIR)
	mkdir -p $@

clean:
	rm -f $(WEB_OUTPUTS) $(STATIC_OUTPUTS) \
		$(BUILD_DIR)/openrouter_transport.js \
		$(WORLD_KNOWLEDGE_RESOURCE_OUTPUTS)
