# Linux-only shared-core harnesses.
#
# These are developer smoke tests for Strappy's portable C code. They compile
# the shared sources directly against the host sqlite3 library and the cJSON
# source vendored in the Altivec tree.

CC := clang
CJSON_DIR ?= /altivec/libs/core/third_party/cJSON
FONTAWESOME_FONT_DIR ?= /altivec/libs/cocoa/Resources/Fonts
SHARED_DIR := ../shared
BUILD_DIR := build-linux
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
WEBVIEW_HARNESS := $(BUILD_DIR)/webview_harness
RESPONSES_HARNESS := $(BUILD_DIR)/responses_harness
PROMPT_GENERATOR_HARNESS := $(BUILD_DIR)/prompt_generator_harness
PROMPT_OUTPUT_DIR ?= $(BUILD_DIR)/system-prompts

DATABASE_QUERY_HARNESS_SOURCES = \
	database_query_harness.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_tools.c \
	$(SHARED_DIR)/strappy_db.c \
	$(SHARED_DIR)/strappy_file_scanner.c \
	$(SHARED_DIR)/strappy_core.c \
	$(SHARED_DIR)/strappy_assistant_sets.c \
	$(CJSON_DIR)/cJSON.c

WEBVIEW_HARNESS_SOURCES = \
	webview_harness.c \
	$(SHARED_DIR)/strappy_webview.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_core.c

RESPONSES_HARNESS_SOURCES = \
	responses_harness.c \
	$(SHARED_DIR)/strappy_client.c \
	$(SHARED_DIR)/strappy_config.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_core.c \
	$(SHARED_DIR)/strappy_db.c \
	$(SHARED_DIR)/strappy_model_catalog.c \
	$(SHARED_DIR)/strappy_prompt.c \
	$(SHARED_DIR)/strappy_quality_policy.c \
	$(SHARED_DIR)/strappy_assistant_sets.c \
	$(SHARED_DIR)/strappy_responses.c \
	$(SHARED_DIR)/strappy_session.c \
	$(SHARED_DIR)/strappy_tools.c \
	$(SHARED_DIR)/strappy_webview.c \
	$(CJSON_DIR)/cJSON.c

PROMPT_GENERATOR_HARNESS_SOURCES = \
	prompt_generator_harness.c \
	$(SHARED_DIR)/strappy_prompt.c \
	$(SHARED_DIR)/strappy_quality_policy.c \
	$(SHARED_DIR)/strappy_tools.c \
	$(SHARED_DIR)/strappy_assistant_sets.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_core.c \
	$(SHARED_DIR)/strappy_db.c \
	$(CJSON_DIR)/cJSON.c

.PHONY: test prompts review-prompts clean

test: $(DATABASE_QUERY_HARNESS) $(WEBVIEW_HARNESS) $(RESPONSES_HARNESS) $(PROMPT_GENERATOR_HARNESS)
	./$(DATABASE_QUERY_HARNESS)
	./$(WEBVIEW_HARNESS)
	./$(RESPONSES_HARNESS)
	./$(PROMPT_GENERATOR_HARNESS) $(SHARED_DIR)/Resources --check

prompts: $(PROMPT_GENERATOR_HARNESS)
	mkdir -p $(PROMPT_OUTPUT_DIR)
	./$(PROMPT_GENERATOR_HARNESS) $(SHARED_DIR)/Resources $(PROMPT_OUTPUT_DIR)

review-prompts: $(PROMPT_GENERATOR_HARNESS)
	./$(PROMPT_GENERATOR_HARNESS) $(SHARED_DIR)/Resources

$(BUILD_DIR):
	mkdir -p $@

$(FONTAWESOME_GENERATED): $(FONTAWESOME_GENERATOR) $(FONTAWESOME_FONT_FILES)
	python3 $(FONTAWESOME_GENERATOR) --font-dir "$(FONTAWESOME_FONT_DIR)" --webview-map "$(FONTAWESOME_WEBVIEW_MAP)"

$(DATABASE_QUERY_HARNESS): $(DATABASE_QUERY_HARNESS_SOURCES) $(FONTAWESOME_GENERATED) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(DATABASE_QUERY_HARNESS_SOURCES) $(SQLITE_LIBS) $(THREAD_LIBS)

$(WEBVIEW_HARNESS): $(WEBVIEW_HARNESS_SOURCES) $(FONTAWESOME_GENERATED) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(WEBVIEW_HARNESS_SOURCES)

$(RESPONSES_HARNESS): $(RESPONSES_HARNESS_SOURCES) $(FONTAWESOME_GENERATED) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(RESPONSES_HARNESS_SOURCES) $(SQLITE_LIBS) $(THREAD_LIBS) -lcurl

$(PROMPT_GENERATOR_HARNESS): $(PROMPT_GENERATOR_HARNESS_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(PROMPT_GENERATOR_HARNESS_SOURCES) $(SQLITE_LIBS) $(THREAD_LIBS)

clean:
	rm -rf $(BUILD_DIR) database_query_harness webview_harness responses_harness
