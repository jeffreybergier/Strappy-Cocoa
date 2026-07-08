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
DATABASE_QUERY_HARNESS := $(BUILD_DIR)/database_query_harness
WEBVIEW_HARNESS := $(BUILD_DIR)/webview_harness
CLIENT_STREAM_HARNESS := $(BUILD_DIR)/client_stream_harness
ASSISTANT_REASONING_HARNESS := $(BUILD_DIR)/assistant_reasoning_harness

DATABASE_QUERY_HARNESS_SOURCES = \
	database_query_harness.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_tools.c \
	$(SHARED_DIR)/strappy_db.c \
	$(SHARED_DIR)/strappy_file_scanner.c \
	$(SHARED_DIR)/strappy_core.c \
	$(CJSON_DIR)/cJSON.c

WEBVIEW_HARNESS_SOURCES = \
	webview_harness.c \
	$(SHARED_DIR)/strappy_webview.c

CLIENT_STREAM_HARNESS_SOURCES = \
	client_stream_harness.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_core.c \
	$(SHARED_DIR)/strappy_tools.c \
	$(SHARED_DIR)/strappy_db.c \
	$(CJSON_DIR)/cJSON.c
CLIENT_STREAM_HARNESS_DEPS = \
	$(SHARED_DIR)/strappy_client.c

ASSISTANT_REASONING_HARNESS_SOURCES = \
	assistant_reasoning_harness.c \
	$(SHARED_DIR)/strappy_client.c \
	$(SHARED_DIR)/strappy_config.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_core.c \
	$(SHARED_DIR)/strappy_db.c \
	$(SHARED_DIR)/strappy_prompt.c \
	$(SHARED_DIR)/strappy_tools.c \
	$(CJSON_DIR)/cJSON.c
ASSISTANT_REASONING_HARNESS_DEPS = \
	$(SHARED_DIR)/strappy_assistant.c

.PHONY: test clean

test: $(DATABASE_QUERY_HARNESS) $(WEBVIEW_HARNESS) $(CLIENT_STREAM_HARNESS) $(ASSISTANT_REASONING_HARNESS)
	./$(DATABASE_QUERY_HARNESS)
	./$(WEBVIEW_HARNESS)
	./$(CLIENT_STREAM_HARNESS)
	./$(ASSISTANT_REASONING_HARNESS)

$(BUILD_DIR):
	mkdir -p $@

$(FONTAWESOME_GENERATED): $(FONTAWESOME_GENERATOR) $(FONTAWESOME_FONT_FILES)
	python3 $(FONTAWESOME_GENERATOR) --font-dir "$(FONTAWESOME_FONT_DIR)" --webview-map "$(FONTAWESOME_WEBVIEW_MAP)"

$(DATABASE_QUERY_HARNESS): $(DATABASE_QUERY_HARNESS_SOURCES) $(FONTAWESOME_GENERATED) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(DATABASE_QUERY_HARNESS_SOURCES) $(SQLITE_LIBS)

$(WEBVIEW_HARNESS): $(WEBVIEW_HARNESS_SOURCES) $(FONTAWESOME_GENERATED) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(WEBVIEW_HARNESS_SOURCES)

$(CLIENT_STREAM_HARNESS): $(CLIENT_STREAM_HARNESS_SOURCES) $(CLIENT_STREAM_HARNESS_DEPS) $(FONTAWESOME_GENERATED) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CLIENT_STREAM_HARNESS_SOURCES) $(SQLITE_LIBS) -lcurl

$(ASSISTANT_REASONING_HARNESS): $(ASSISTANT_REASONING_HARNESS_SOURCES) $(ASSISTANT_REASONING_HARNESS_DEPS) $(FONTAWESOME_GENERATED) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(ASSISTANT_REASONING_HARNESS_SOURCES) $(SQLITE_LIBS) -lcurl

clean:
	rm -rf $(BUILD_DIR) database_query_harness webview_harness client_stream_harness assistant_reasoning_harness
