# Linux-only shared-core harnesses.
#
# These are developer smoke tests for Strappy's portable C code. They compile
# the shared sources directly against the host sqlite3 library and the cJSON
# source vendored in the Altivec tree.

CC := clang
NODE ?= node
ALTIVEC_ROOT ?= /altivec
CJSON_DIR ?= $(ALTIVEC_ROOT)/libs/core/third_party/cJSON
FONTAWESOME_FONT_DIR ?= $(ALTIVEC_ROOT)/libs/cocoa/build-phone/Resources/Fonts
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
STRAPPY_PLATFORM_SOURCES = \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_calendar.c
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
OTHER_OPENROUTER_COMPATIBILITY_PROBE := \
	$(BUILD_DIR)/other_openrouter_compatibility_probe
PROVIDER_REGISTRY_HARNESS := $(BUILD_DIR)/provider_registry_harness
CALENDAR_HARNESS := $(BUILD_DIR)/calendar_harness
STRAPPY_CHATGPT_PROBE_CREDENTIAL_PATH ?= \
	$(abspath ../../.env.chatgpt.json)
STRAPPY_OTHER_PROBE_ENV_PATH ?= $(abspath ../../.env)
PROMPT_OUTPUT_DIR ?= $(BUILD_DIR)/system-prompts

ifeq ($(strip $(TMPDIR)),)
TMPDIR := $(abspath $(TEST_DIR))
endif
export TMPDIR

DATABASE_QUERY_HARNESS_SOURCES = \
	database_query_harness.c \
	$(STRAPPY_PLATFORM_SOURCES) \
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
	$(STRAPPY_PLATFORM_SOURCES) \
	$(SHARED_DIR)/strappy_config.c \
	$(STRAPPY_DB_SOURCES) \
	$(SHARED_DIR)/strappy_core.c \
	$(SHARED_DIR)/strappy_assistant_sets.c \
	$(CJSON_DIR)/cJSON.c

WEBVIEW_HARNESS_SOURCES = \
	webview_harness.c \
	$(SHARED_DIR)/strappy_webview.c \
	$(STRAPPY_PLATFORM_SOURCES) \
	$(SHARED_DIR)/strappy_core.c \
	$(CJSON_DIR)/cJSON.c

RESPONSES_HARNESS_SOURCES = \
	responses_harness.c \
	$(SHARED_DIR)/strappy_client.c \
	$(SHARED_DIR)/strappy_sse.c \
	$(SHARED_DIR)/strappy_identity.c \
	$(SHARED_DIR)/strappy_config.c \
	$(STRAPPY_PLATFORM_SOURCES) \
	$(SHARED_DIR)/strappy_core.c \
	$(STRAPPY_DB_SOURCES) \
	$(SHARED_DIR)/strappy_model_catalog.c \
	$(SHARED_DIR)/strappy_prompt.c \
	$(SHARED_DIR)/strappy_quality_policy.c \
	$(SHARED_DIR)/strappy_assistant_sets.c \
	$(SHARED_DIR)/strappy_debug_capture.c \
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
	$(STRAPPY_PLATFORM_SOURCES) \
	$(SHARED_DIR)/strappy_core.c \
	$(STRAPPY_DB_SOURCES) \
	$(CJSON_DIR)/cJSON.c

SCANNER_PROFILE_HARNESS_SOURCES = \
	scanner_profile_harness.c \
	$(SHARED_DIR)/strappy_file_scanner.c \
	$(STRAPPY_PLATFORM_SOURCES) \
	$(SHARED_DIR)/strappy_core.c

OPENAI_OAUTH_POC_SOURCES = \
	openai_oauth_poc.c \
	$(SHARED_DIR)/strappy_identity.c \
	$(STRAPPY_PLATFORM_SOURCES) \
	$(SHARED_DIR)/strappy_core.c \
	$(CJSON_DIR)/cJSON.c

OPENAI_OAUTH_HARNESS_SOURCES = \
	openai_oauth_harness.c \
	$(SHARED_DIR)/strappy_openai_oauth.c \
	$(SHARED_DIR)/strappy_identity.c \
	$(STRAPPY_PLATFORM_SOURCES) \
	$(SHARED_DIR)/strappy_core.c \
	$(CJSON_DIR)/cJSON.c

CHATGPT_ADAPTER_HARNESS_SOURCES = \
	chatgpt_adapter_harness.c \
	$(SHARED_DIR)/strappy_client.c \
	$(SHARED_DIR)/strappy_sse.c \
	$(SHARED_DIR)/strappy_identity.c \
	$(SHARED_DIR)/strappy_config.c \
	$(SHARED_DIR)/strappy_provider.c \
	$(STRAPPY_PLATFORM_SOURCES) \
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
	$(STRAPPY_PLATFORM_SOURCES) \
	$(SHARED_DIR)/strappy_core.c \
	$(CJSON_DIR)/cJSON.c

OTHER_OPENROUTER_COMPATIBILITY_PROBE_SOURCES = \
	other_openrouter_compatibility_probe.c \
	$(SHARED_DIR)/strappy_client.c \
	$(SHARED_DIR)/strappy_sse.c \
	$(SHARED_DIR)/strappy_identity.c \
	$(SHARED_DIR)/strappy_config.c \
	$(SHARED_DIR)/strappy_provider.c \
	$(STRAPPY_PLATFORM_SOURCES) \
	$(SHARED_DIR)/strappy_core.c \
	$(CJSON_DIR)/cJSON.c

PROVIDER_REGISTRY_HARNESS_SOURCES = \
	provider_registry_harness.c \
	$(SHARED_DIR)/strappy_provider.c \
	$(SHARED_DIR)/strappy_core.c

CALENDAR_HARNESS_SOURCES = \
	calendar_harness.c \
	$(SHARED_DIR)/strappy_calendar.c \
	$(SHARED_DIR)/strappy_core.c

ifeq ($(shell uname -s),Darwin)
CPPFLAGS += -D_DARWIN_C_SOURCE
PLATFORM_LIBS := -framework CoreFoundation
endif

.PHONY: test live-test require-live-test-opt-in scanner-test prompts review-prompts manual-openai-oauth \
	manual-chatgpt-compatibility chatgpt-credential-cache-test \
	openai-web-search-live-test other-openrouter-live-test \
	openrouter-live-test openai-oauth-test \
	openai-oauth-poc-self-test clean

test: $(PROVIDER_REGISTRY_HARNESS) $(SCANNER_PROFILE_HARNESS) $(DATABASE_QUERY_HARNESS) $(BASH_HARNESS) $(WEBVIEW_HARNESS) $(RESPONSES_HARNESS) $(PROMPT_GENERATOR_HARNESS) $(OPENAI_OAUTH_HARNESS) $(CHATGPT_ADAPTER_HARNESS) $(CHATGPT_COMPATIBILITY_PROBE) $(CALENDAR_HARNESS) | $(TEST_DIR)
	@echo "--- Running Linux shared-core test suite ---"
	@echo "No live provider requests are made by this target."
	@echo " [1/12] RUN  provider_registry_harness"
	./$(PROVIDER_REGISTRY_HARNESS)
	@echo " [1/12] PASS provider_registry_harness"
	@echo " [2/12] RUN  scanner_profile_harness"
	./$(SCANNER_PROFILE_HARNESS)
	@echo " [2/12] PASS scanner_profile_harness"
	@echo " [3/12] RUN  database_query_harness"
	./$(DATABASE_QUERY_HARNESS)
	@echo " [3/12] PASS database_query_harness"
	@echo " [4/12] RUN  bash_harness"
	./$(BASH_HARNESS)
	@echo " [4/12] PASS bash_harness"
	@echo " [5/12] RUN  webview_harness"
	STRAPPY_WEBVIEW_PAGE_OUTPUT=$(WEBVIEW_PAGE_FIXTURE) ./$(WEBVIEW_HARNESS)
	@echo " [5/12] PASS webview_harness"
	@echo " [6/12] RUN  webview_behavior_harness.js"
	$(NODE) webview_behavior_harness.js $(WEBVIEW_PAGE_FIXTURE)
	@echo " [6/12] PASS webview_behavior_harness.js"
	@echo " [7/12] RUN  responses_harness"
	./$(RESPONSES_HARNESS)
	@echo " [7/12] PASS responses_harness"
	@echo " [8/12] RUN  prompt_generator_harness"
	./$(PROMPT_GENERATOR_HARNESS) $(SHARED_DIR)/Resources --check
	@echo " [8/12] PASS prompt_generator_harness"
	@echo " [9/12] RUN  openai_oauth_harness"
	./$(OPENAI_OAUTH_HARNESS)
	@echo " [9/12] PASS openai_oauth_harness"
	@echo " [10/12] RUN  chatgpt_adapter_harness"
	./$(CHATGPT_ADAPTER_HARNESS)
	@echo " [10/12] PASS chatgpt_adapter_harness"
	@echo " [11/12] RUN  chatgpt credential-cache self-test"
	./$(CHATGPT_COMPATIBILITY_PROBE) --credential-cache-self-test
	@echo " [11/12] PASS chatgpt credential-cache self-test"
	@echo " [12/12] RUN  calendar_harness"
	./$(CALENDAR_HARNESS)
	@echo " [12/12] PASS calendar_harness"
	@echo "--- PASS: all 12 offline Linux shared-core checks completed ---"

require-live-test-opt-in:
	@test "$$STRAPPY_ALLOW_LIVE_PROVIDER_TESTS" = "YES" || \
	  (echo "Refusing live provider requests. Set STRAPPY_ALLOW_LIVE_PROVIDER_TESTS=YES explicitly." >&2; exit 1)

live-test: test
	+$(MAKE) openai-web-search-live-test

scanner-test: $(SCANNER_PROFILE_HARNESS) | $(TEST_DIR)
	./$(SCANNER_PROFILE_HARNESS)

# Explicitly manual: these require browser interaction and live ChatGPT usage.
# The standalone OAuth POC never persists tokens. The compatibility probe uses
# an ignored, owner-only cache so repeated runs avoid unnecessary OAuth calls.
manual-openai-oauth: require-live-test-opt-in $(OPENAI_OAUTH_POC)
	./$(OPENAI_OAUTH_POC)

manual-chatgpt-compatibility: require-live-test-opt-in $(CHATGPT_COMPATIBILITY_PROBE)
	STRAPPY_CHATGPT_PROBE_CREDENTIAL_PATH="$(STRAPPY_CHATGPT_PROBE_CREDENTIAL_PATH)" ./$(CHATGPT_COMPATIBILITY_PROBE)

chatgpt-credential-cache-test: $(CHATGPT_COMPATIBILITY_PROBE)
	./$(CHATGPT_COMPATIBILITY_PROBE) --credential-cache-self-test

openai-web-search-live-test: require-live-test-opt-in $(CHATGPT_COMPATIBILITY_PROBE)
	STRAPPY_CHATGPT_PROBE_CREDENTIAL_PATH="$(STRAPPY_CHATGPT_PROBE_CREDENTIAL_PATH)" ./$(CHATGPT_COMPATIBILITY_PROBE) --web-search-live-if-configured

other-openrouter-live-test: require-live-test-opt-in $(OTHER_OPENROUTER_COMPATIBILITY_PROBE)
	env -u APIENDPOINT -u APITOKEN \
	  STRAPPY_OTHER_PROBE_ENV_PATH="$(STRAPPY_OTHER_PROBE_ENV_PATH)" \
	  ./$(OTHER_OPENROUTER_COMPATIBILITY_PROBE)

openrouter-live-test: require-live-test-opt-in $(OTHER_OPENROUTER_COMPATIBILITY_PROBE)
	env -u APIENDPOINT -u APITOKEN \
	  STRAPPY_OTHER_PROBE_ENV_PATH="$(STRAPPY_OTHER_PROBE_ENV_PATH)" \
	  ./$(OTHER_OPENROUTER_COMPATIBILITY_PROBE) --openrouter

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
	$(CC) $(CPPFLAGS) -DSTRAPPY_ENABLE_LOOPBACK_HTTP_TESTS=1 $(CFLAGS) -o $@ $(RESPONSES_HARNESS_SOURCES) $(SQLITE_LIBS) $(THREAD_LIBS) $(PLATFORM_LIBS) -lcurl

$(PROMPT_GENERATOR_HARNESS): $(PROMPT_GENERATOR_HARNESS_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(PROMPT_GENERATOR_HARNESS_SOURCES) $(SQLITE_LIBS) $(THREAD_LIBS) $(PLATFORM_LIBS) -lcurl

$(SCANNER_PROFILE_HARNESS): $(SCANNER_PROFILE_HARNESS_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SCANNER_PROFILE_HARNESS_SOURCES) $(SQLITE_LIBS) $(PLATFORM_LIBS)

$(OPENAI_OAUTH_POC): $(OPENAI_OAUTH_POC_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(OPENAI_OAUTH_POC_SOURCES) $(PLATFORM_LIBS) -lcurl

$(OPENAI_OAUTH_HARNESS): $(OPENAI_OAUTH_HARNESS_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -DSTRAPPY_ENABLE_LOOPBACK_HTTP_TESTS=1 $(CFLAGS) -o $@ $(OPENAI_OAUTH_HARNESS_SOURCES) $(PLATFORM_LIBS) -lcurl

$(CHATGPT_ADAPTER_HARNESS): $(CHATGPT_ADAPTER_HARNESS_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -DSTRAPPY_ENABLE_LOOPBACK_HTTP_TESTS=1 $(CFLAGS) -o $@ $(CHATGPT_ADAPTER_HARNESS_SOURCES) $(PLATFORM_LIBS) -lcurl

$(CHATGPT_COMPATIBILITY_PROBE): $(CHATGPT_COMPATIBILITY_PROBE_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CHATGPT_COMPATIBILITY_PROBE_SOURCES) $(PLATFORM_LIBS) -lcurl

$(OTHER_OPENROUTER_COMPATIBILITY_PROBE): $(OTHER_OPENROUTER_COMPATIBILITY_PROBE_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(OTHER_OPENROUTER_COMPATIBILITY_PROBE_SOURCES) $(PLATFORM_LIBS) -lcurl

$(PROVIDER_REGISTRY_HARNESS): $(PROVIDER_REGISTRY_HARNESS_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(PROVIDER_REGISTRY_HARNESS_SOURCES) $(PLATFORM_LIBS)

$(CALENDAR_HARNESS): $(CALENDAR_HARNESS_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CALENDAR_HARNESS_SOURCES) $(PLATFORM_LIBS)

clean:
	rm -rf $(BUILD_DIR) $(TEST_DIR) database_query_harness webview_harness responses_harness
