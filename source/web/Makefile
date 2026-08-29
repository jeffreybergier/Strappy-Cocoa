# Browser-only WebAssembly build. Run this Makefile through the wasm-build
# Compose service so Emscripten never needs to be installed on the host.

EMCC := emcc
NODE := $(shell command -v node 2>/dev/null)
SHARED_DIR := ../shared
RESOURCE_DIR := $(SHARED_DIR)/Resources
BUILD_DIR := build-release
RESOURCE_OUTPUT_DIR := $(BUILD_DIR)/Resources
MODULE_NAME := strappy
SQLITE_VERSION := 3530400
SQLITE_BUILD_DIR := build-sqlite
SQLITE_SOURCE_DIR := $(SQLITE_BUILD_DIR)/sqlite-src-$(SQLITE_VERSION)
SQLITE_WASM_DIR := $(SQLITE_SOURCE_DIR)/ext/wasm
SQLITE_MODULE_JS := $(SQLITE_WASM_DIR)/jswasm/esm/sqlite3.mjs
SQLITE_MODULE_WASM := $(SQLITE_WASM_DIR)/jswasm/esm/sqlite3.wasm

WEB_SOURCES := \
	$(SHARED_DIR)/strappy_core.c \
	$(SHARED_DIR)/strappy_provider.c \
	$(SHARED_DIR)/strappy_platform_profile.c \
	$(SHARED_DIR)/strappy_assistant_sets.c \
	$(SHARED_DIR)/strappy_tools.c \
	$(SHARED_DIR)/strappy_client.c \
	$(SHARED_DIR)/strappy_client_web.c \
	$(SHARED_DIR)/strappy_db_connection_web.c \
	$(SHARED_DIR)/strappy_db.c \
	$(SHARED_DIR)/strappy_db_catalog.c \
	$(SHARED_DIR)/strappy_db_sessions.c \
	$(SHARED_DIR)/strappy_db_responses.c \
	$(SHARED_DIR)/strappy_model_catalog.c \
	$(SHARED_DIR)/strappy_prompt.c \
	$(SHARED_DIR)/strappy_quality_policy.c \
	$(SHARED_DIR)/strappy_responses.c \
	$(SHARED_DIR)/strappy_session.c \
	$(SHARED_DIR)/strappy_skills.c \
	$(SHARED_DIR)/strappy_webview.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_calendar.c \
	$(SHARED_DIR)/strappy_debug_capture.c \
	strappy_sse_web.c \
	strappy_web_client.c \
	strappy_web_appearance.c \
	strappy_web_database.c \
	strappy_web_capabilities.c \
	strappy_web_conversation.c

WEB_HEADERS := \
	$(SHARED_DIR)/strappy_core.h \
	$(SHARED_DIR)/strappy_palette.h \
	$(SHARED_DIR)/strappy_provider.h \
	$(SHARED_DIR)/strappy_platform_profile.h \
	$(SHARED_DIR)/strappy_assistant_sets.h \
	$(SHARED_DIR)/strappy_tools.h \
	$(SHARED_DIR)/strappy_config.h \
	$(SHARED_DIR)/strappy_client.h \
	$(SHARED_DIR)/strappy_client_transport.h \
	$(SHARED_DIR)/strappy_sse.h \
	$(SHARED_DIR)/strappy_db.h \
	$(SHARED_DIR)/strappy_db_internal.h

WEB_BUILD_INPUTS := Makefile prepare_sqlite_wasm.sh strappy_client_fetch.js \
	strappy_sqlite.make strappy_sqlite_exports.txt strappy_sqlite_post.js \
	strappy_sqlite_pre.js \
	$(WEB_SOURCES) $(WEB_HEADERS)

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
WORKER_CLIENT_TEST_SCRIPT := worker_transport_smoke_test.mjs
WORKER_CONVERSATION_TEST_SCRIPT := worker_conversation_smoke_test.mjs
WORKER_ERROR_TEST_SCRIPT := worker_error_smoke_test.mjs

CPPFLAGS := -I$(SHARED_DIR)
COMMON_CFLAGS := \
	-std=c99 \
	-Wall \
	-Wextra \
	-Wconversion \
	-Wsign-conversion \
	-Wfloat-conversion
# SQLite's canonical Wasm glue preserves external export names at -O1 without
# requiring a separate WABT stripping pass in the pinned Emscripten image.
RELEASE_CFLAGS := -O1
.DEFAULT_GOAL := release

.PHONY: clean distclean release test offline-test

release: $(WEB_OUTPUTS) $(STATIC_OUTPUTS) $(WORLD_KNOWLEDGE_RESOURCE_OUTPUTS)

test: offline-test

offline-test: release
	$(NODE) $(CLIENT_TEST_SCRIPT)
	$(NODE) $(WORKER_CLIENT_TEST_SCRIPT)
	$(NODE) $(WORKER_CONVERSATION_TEST_SCRIPT)
	$(NODE) $(WORKER_ERROR_TEST_SCRIPT)
	$(NODE) $(TEST_SCRIPT)

# Emscripten creates the JavaScript loader and Wasm binary in one invocation.
$(WEB_OUTPUTS) &: $(WEB_BUILD_INPUTS) | $(BUILD_DIR)
	sh prepare_sqlite_wasm.sh $(SQLITE_BUILD_DIR)
	$(MAKE) -C $(SQLITE_WASM_DIR) -f GNUmakefile \
		-f $(abspath strappy_sqlite.make) clean
	$(MAKE) -C $(SQLITE_WASM_DIR) -f GNUmakefile \
		-f $(abspath strappy_sqlite.make) b-esm emcc_opt=$(RELEASE_CFLAGS)
	cp $(SQLITE_MODULE_JS) $(WEB_JAVASCRIPT)
	cp $(SQLITE_MODULE_WASM) $(WEB_WASM)

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

distclean: clean
	rm -rf $(SQLITE_BUILD_DIR)
