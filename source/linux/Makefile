# Linux-only shared-core harnesses.
#
# These are developer smoke tests for Strappy's portable C code. They compile
# the shared sources directly against the host sqlite3 library and the cJSON
# source vendored in the Altivec tree.

CC := clang
NODE ?= node
CJSON_DIR ?= /altivec/libs/core/third_party/cJSON
FONTAWESOME_FONT_DIR ?= /altivec/libs/cocoa/Resources/Fonts
SHARED_DIR := ../shared
STRAPPY_INFO_PLIST_PATH ?= $(abspath ../macOS/Info.plist)
BUILD_DIR := build-linux
TEST_DIR := build-test
export STRAPPY_INFO_PLIST_PATH
STRAPPY_DB_SOURCES = \
	$(SHARED_DIR)/strappy_provider.c \
	$(SHARED_DIR)/strappy_db.c \
	$(SHARED_DIR)/strappy_db_catalog.c \
	$(SHARED_DIR)/strappy_db_sessions.c \
	$(SHARED_DIR)/strappy_db_responses.c
FONTAWESOME_GENERATOR := $(SHARED_DIR)/tools/generate_fontawesome_webview_map.py
FONTAWESOME_ICONS_RESOURCE := $(SHARED_DIR)/Resources/FontAwesomeIcons.json
FONTAWESOME_WEBVIEW_MAP := $(SHARED_DIR)/strappy_fontawesome_icons.inc
FONTAWESOME_GENERATED := $(FONTAWESOME_WEBVIEW_MAP)
FONTAWESOME_FONT_FILES := \
	$(FONTAWESOME_FONT_DIR)/FA7-Solid-900.otf \
	$(FONTAWESOME_FONT_DIR)/FA7-Regular-400.otf \
	$(FONTAWESOME_FONT_DIR)/FA7-Brands-400.otf

CPPFLAGS += -I$(SHARED_DIR) -I$(CJSON_DIR)
CFLAGS ?= -std=c99 -Wall -Wextra -Wconversion -Wsign-conversion -Wfloat-conversion
SQLITE_LIBS ?= -lsqlite3
THREAD_LIBS ?= -pthread
DATABASE_QUERY_HARNESS := $(BUILD_DIR)/database_query_harness
BASH_HARNESS := $(BUILD_DIR)/bash_harness
WEBVIEW_HARNESS := $(BUILD_DIR)/webview_harness
WEBVIEW_PAGE_FIXTURE := $(BUILD_DIR)/webview-page.html
RESPONSES_HARNESS := $(BUILD_DIR)/responses_harness
PROMPT_GENERATOR_HARNESS := $(BUILD_DIR)/prompt_generator_harness
SCANNER_PROFILE_HARNESS := $(BUILD_DIR)/scanner_profile_harness
OPENAI_OAUTH_POC := $(BUILD_DIR)/openai_oauth_poc
OPENAI_OAUTH_HARNESS := $(BUILD_DIR)/openai_oauth_harness
CHATGPT_ADAPTER_HARNESS := $(BUILD_DIR)/chatgpt_adapter_harness
CHATGPT_COMPATIBILITY_PROBE := $(BUILD_DIR)/chatgpt_compatibility_probe
STRAPPY_CHATGPT_PROBE_CREDENTIAL_PATH ?= \
	$(abspath ../../.env.chatgpt.json)
PROMPT_OUTPUT_DIR ?= $(BUILD_DIR)/system-prompts

ifeq ($(strip $(TMPDIR)),)
TMPDIR := $(abspath $(TEST_DIR))
endif
export TMPDIR

DATABASE_QUERY_HARNESS_SOURCES = \
	database_query_harness.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_client.c \
	$(SHARED_DIR)/strappy_sse.c \
	$(SHARED_DIR)/strappy_identity.c \
	$(SHARED_DIR)/strappy_config.c \
	$(SHARED_DIR)/strappy_model_catalog.c \
	$(SHARED_DIR)/strappy_tools.c \
	$(SHARED_DIR)/strappy_skills.c \
	$(SHARED_DIR)/strappy_study.c \
	$(SHARED_DIR)/strappy_bash.c \
	$(SHARED_DIR)/strappy_file_read.c \
	$(SHARED_DIR)/strappy_file_mutation.c \
	$(SHARED_DIR)/strappy_file_write.c \
	$(SHARED_DIR)/strappy_file_edit.c \
	$(STRAPPY_DB_SOURCES) \
	$(SHARED_DIR)/strappy_file_scanner.c \
	$(SHARED_DIR)/strappy_core.c \
	$(SHARED_DIR)/strappy_assistant_sets.c \
	$(CJSON_DIR)/cJSON.c

BASH_HARNESS_SOURCES = \
	bash_harness.c \
	$(SHARED_DIR)/strappy_bash.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_config.c \
	$(STRAPPY_DB_SOURCES) \
	$(SHARED_DIR)/strappy_core.c \
	$(SHARED_DIR)/strappy_assistant_sets.c \
	$(CJSON_DIR)/cJSON.c

WEBVIEW_HARNESS_SOURCES = \
	webview_harness.c \
	$(SHARED_DIR)/strappy_webview.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_core.c \
	$(CJSON_DIR)/cJSON.c

RESPONSES_HARNESS_SOURCES = \
	responses_harness.c \
	$(SHARED_DIR)/strappy_client.c \
	$(SHARED_DIR)/strappy_sse.c \
	$(SHARED_DIR)/strappy_identity.c \
	$(SHARED_DIR)/strappy_config.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_core.c \
	$(STRAPPY_DB_SOURCES) \
	$(SHARED_DIR)/strappy_model_catalog.c \
	$(SHARED_DIR)/strappy_prompt.c \
	$(SHARED_DIR)/strappy_quality_policy.c \
	$(SHARED_DIR)/strappy_assistant_sets.c \
	$(SHARED_DIR)/strappy_responses.c \
	$(SHARED_DIR)/strappy_session.c \
	$(SHARED_DIR)/strappy_tools.c \
	$(SHARED_DIR)/strappy_skills.c \
	$(SHARED_DIR)/strappy_study.c \
	$(SHARED_DIR)/strappy_bash.c \
	$(SHARED_DIR)/strappy_file_read.c \
	$(SHARED_DIR)/strappy_file_mutation.c \
	$(SHARED_DIR)/strappy_file_write.c \
	$(SHARED_DIR)/strappy_file_edit.c \
	$(SHARED_DIR)/strappy_webview.c \
	$(CJSON_DIR)/cJSON.c

PROMPT_GENERATOR_HARNESS_SOURCES = \
	prompt_generator_harness.c \
	$(SHARED_DIR)/strappy_prompt.c \
	$(SHARED_DIR)/strappy_quality_policy.c \
	$(SHARED_DIR)/strappy_tools.c \
	$(SHARED_DIR)/strappy_skills.c \
	$(SHARED_DIR)/strappy_study.c \
	$(SHARED_DIR)/strappy_client.c \
	$(SHARED_DIR)/strappy_sse.c \
	$(SHARED_DIR)/strappy_identity.c \
	$(SHARED_DIR)/strappy_config.c \
	$(SHARED_DIR)/strappy_bash.c \
	$(SHARED_DIR)/strappy_file_read.c \
	$(SHARED_DIR)/strappy_file_mutation.c \
	$(SHARED_DIR)/strappy_file_write.c \
	$(SHARED_DIR)/strappy_file_edit.c \
	$(SHARED_DIR)/strappy_assistant_sets.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_core.c \
	$(STRAPPY_DB_SOURCES) \
	$(CJSON_DIR)/cJSON.c

SCANNER_PROFILE_HARNESS_SOURCES = \
	scanner_profile_harness.c \
	$(SHARED_DIR)/strappy_file_scanner.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_core.c

OPENAI_OAUTH_POC_SOURCES = \
	openai_oauth_poc.c \
	$(SHARED_DIR)/strappy_identity.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_core.c \
	$(CJSON_DIR)/cJSON.c

OPENAI_OAUTH_HARNESS_SOURCES = \
	openai_oauth_harness.c \
	$(SHARED_DIR)/strappy_openai_oauth.c \
	$(SHARED_DIR)/strappy_identity.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_core.c \
	$(CJSON_DIR)/cJSON.c

CHATGPT_ADAPTER_HARNESS_SOURCES = \
	chatgpt_adapter_harness.c \
	$(SHARED_DIR)/strappy_client.c \
	$(SHARED_DIR)/strappy_sse.c \
	$(SHARED_DIR)/strappy_identity.c \
	$(SHARED_DIR)/strappy_config.c \
	$(SHARED_DIR)/strappy_provider.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_core.c \
	$(CJSON_DIR)/cJSON.c

CHATGPT_COMPATIBILITY_PROBE_SOURCES = \
	chatgpt_compatibility_probe.c \
	$(SHARED_DIR)/strappy_client.c \
	$(SHARED_DIR)/strappy_sse.c \
	$(SHARED_DIR)/strappy_openai_oauth.c \
	$(SHARED_DIR)/strappy_identity.c \
	$(SHARED_DIR)/strappy_config.c \
	$(SHARED_DIR)/strappy_provider.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_core.c \
	$(CJSON_DIR)/cJSON.c

ifeq ($(shell uname -s),Darwin)
CPPFLAGS += -D_DARWIN_C_SOURCE
PLATFORM_LIBS := -framework CoreFoundation
endif

.PHONY: test scanner-test prompts review-prompts manual-openai-oauth \
	manual-chatgpt-compatibility chatgpt-credential-cache-test \
	openai-web-search-live-test openai-oauth-test \
	openai-oauth-poc-self-test clean

test: $(SCANNER_PROFILE_HARNESS) $(DATABASE_QUERY_HARNESS) $(BASH_HARNESS) $(WEBVIEW_HARNESS) $(RESPONSES_HARNESS) $(PROMPT_GENERATOR_HARNESS) $(OPENAI_OAUTH_HARNESS) $(CHATGPT_ADAPTER_HARNESS) $(CHATGPT_COMPATIBILITY_PROBE) | $(TEST_DIR)
	@echo "--- Running Linux shared-core test suite ---"
	@echo " [1/11] RUN  scanner_profile_harness"
	./$(SCANNER_PROFILE_HARNESS)
	@echo " [1/11] PASS scanner_profile_harness"
	@echo " [2/11] RUN  database_query_harness"
	./$(DATABASE_QUERY_HARNESS)
	@echo " [2/11] PASS database_query_harness"
	@echo " [3/11] RUN  bash_harness"
	./$(BASH_HARNESS)
	@echo " [3/11] PASS bash_harness"
	@echo " [4/11] RUN  webview_harness"
	STRAPPY_WEBVIEW_PAGE_OUTPUT=$(WEBVIEW_PAGE_FIXTURE) ./$(WEBVIEW_HARNESS)
	@echo " [4/11] PASS webview_harness"
	@echo " [5/11] RUN  webview_behavior_harness.js"
	$(NODE) webview_behavior_harness.js $(WEBVIEW_PAGE_FIXTURE)
	@echo " [5/11] PASS webview_behavior_harness.js"
	@echo " [6/11] RUN  responses_harness"
	./$(RESPONSES_HARNESS)
	@echo " [6/11] PASS responses_harness"
	@echo " [7/11] RUN  prompt_generator_harness"
	./$(PROMPT_GENERATOR_HARNESS) $(SHARED_DIR)/Resources --check
	@echo " [7/11] PASS prompt_generator_harness"
	@echo " [8/11] RUN  openai_oauth_harness"
	./$(OPENAI_OAUTH_HARNESS)
	@echo " [8/11] PASS openai_oauth_harness"
	@echo " [9/11] RUN  chatgpt_adapter_harness"
	./$(CHATGPT_ADAPTER_HARNESS)
	@echo " [9/11] PASS chatgpt_adapter_harness"
	@echo " [10/11] RUN  chatgpt credential-cache self-test"
	./$(CHATGPT_COMPATIBILITY_PROBE) --credential-cache-self-test
	@echo " [10/11] PASS chatgpt credential-cache self-test"
	@echo " [11/11] RUN  optional LUNA native web-search live check"
	STRAPPY_CHATGPT_PROBE_CREDENTIAL_PATH="$(STRAPPY_CHATGPT_PROBE_CREDENTIAL_PATH)" ./$(CHATGPT_COMPATIBILITY_PROBE) --web-search-live-if-configured
	@echo " [11/11] PASS optional LUNA native web-search live check"
	@echo "--- PASS: all 11 Linux shared-core checks completed ---"

scanner-test: $(SCANNER_PROFILE_HARNESS) | $(TEST_DIR)
	./$(SCANNER_PROFILE_HARNESS)

# Explicitly manual: these require browser interaction and live ChatGPT usage.
# The standalone OAuth POC never persists tokens. The compatibility probe uses
# an ignored, owner-only cache so repeated runs avoid unnecessary OAuth calls.
manual-openai-oauth: $(OPENAI_OAUTH_POC)
	./$(OPENAI_OAUTH_POC)

manual-chatgpt-compatibility: $(CHATGPT_COMPATIBILITY_PROBE)
	STRAPPY_CHATGPT_PROBE_CREDENTIAL_PATH="$(STRAPPY_CHATGPT_PROBE_CREDENTIAL_PATH)" ./$(CHATGPT_COMPATIBILITY_PROBE)

chatgpt-credential-cache-test: $(CHATGPT_COMPATIBILITY_PROBE)
	./$(CHATGPT_COMPATIBILITY_PROBE) --credential-cache-self-test

openai-web-search-live-test: $(CHATGPT_COMPATIBILITY_PROBE)
	STRAPPY_CHATGPT_PROBE_CREDENTIAL_PATH="$(STRAPPY_CHATGPT_PROBE_CREDENTIAL_PATH)" ./$(CHATGPT_COMPATIBILITY_PROBE) --web-search-live-if-configured

openai-oauth-poc-self-test: $(OPENAI_OAUTH_POC)
	./$(OPENAI_OAUTH_POC) --self-test

openai-oauth-test: $(OPENAI_OAUTH_HARNESS)
	./$(OPENAI_OAUTH_HARNESS)

prompts: $(PROMPT_GENERATOR_HARNESS)
	mkdir -p $(PROMPT_OUTPUT_DIR)
	./$(PROMPT_GENERATOR_HARNESS) $(SHARED_DIR)/Resources $(PROMPT_OUTPUT_DIR)

review-prompts: $(PROMPT_GENERATOR_HARNESS)
	./$(PROMPT_GENERATOR_HARNESS) $(SHARED_DIR)/Resources

$(BUILD_DIR): | $(TEST_DIR)
	mkdir -p $@

$(TEST_DIR):
	mkdir -p $@

$(FONTAWESOME_GENERATED): $(FONTAWESOME_GENERATOR) $(FONTAWESOME_FONT_FILES)
	python3 $(FONTAWESOME_GENERATOR) --font-dir "$(FONTAWESOME_FONT_DIR)" --webview-map "$(FONTAWESOME_WEBVIEW_MAP)"

$(DATABASE_QUERY_HARNESS): $(DATABASE_QUERY_HARNESS_SOURCES) $(FONTAWESOME_GENERATED) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(DATABASE_QUERY_HARNESS_SOURCES) $(SQLITE_LIBS) $(THREAD_LIBS) $(PLATFORM_LIBS) -lcurl

$(BASH_HARNESS): $(BASH_HARNESS_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(BASH_HARNESS_SOURCES) $(SQLITE_LIBS) $(THREAD_LIBS) $(PLATFORM_LIBS)

$(WEBVIEW_HARNESS): $(WEBVIEW_HARNESS_SOURCES) $(FONTAWESOME_GENERATED) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(WEBVIEW_HARNESS_SOURCES) $(PLATFORM_LIBS)

$(RESPONSES_HARNESS): $(RESPONSES_HARNESS_SOURCES) $(FONTAWESOME_GENERATED) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(RESPONSES_HARNESS_SOURCES) $(SQLITE_LIBS) $(THREAD_LIBS) $(PLATFORM_LIBS) -lcurl

$(PROMPT_GENERATOR_HARNESS): $(PROMPT_GENERATOR_HARNESS_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(PROMPT_GENERATOR_HARNESS_SOURCES) $(SQLITE_LIBS) $(THREAD_LIBS) $(PLATFORM_LIBS) -lcurl

$(SCANNER_PROFILE_HARNESS): $(SCANNER_PROFILE_HARNESS_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SCANNER_PROFILE_HARNESS_SOURCES) $(SQLITE_LIBS) $(PLATFORM_LIBS)

$(OPENAI_OAUTH_POC): $(OPENAI_OAUTH_POC_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(OPENAI_OAUTH_POC_SOURCES) $(PLATFORM_LIBS) -lcurl

$(OPENAI_OAUTH_HARNESS): $(OPENAI_OAUTH_HARNESS_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(OPENAI_OAUTH_HARNESS_SOURCES) $(PLATFORM_LIBS) -lcurl

$(CHATGPT_ADAPTER_HARNESS): $(CHATGPT_ADAPTER_HARNESS_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CHATGPT_ADAPTER_HARNESS_SOURCES) $(PLATFORM_LIBS) -lcurl

$(CHATGPT_COMPATIBILITY_PROBE): $(CHATGPT_COMPATIBILITY_PROBE_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CHATGPT_COMPATIBILITY_PROBE_SOURCES) $(PLATFORM_LIBS) -lcurl

clean:
	rm -rf $(BUILD_DIR) $(TEST_DIR) database_query_harness webview_harness responses_harness
