# Manual, live OpenRouter evaluation harness.
#
# This Makefile is intentionally not included by ../Makefile. The default
# target only builds the runner; network requests require an explicit
# `make run CONFIRM_LIVE=yes`.

CC := clang
CJSON_DIR ?= /altivec/libs/core/third_party/cJSON
SHARED_DIR := ../../shared
BUILD_DIR := build-linux
RUNNER := $(BUILD_DIR)/hill_climbing
MEDIA_DB ?= private/Media/iTunes_Control/iTunes/MediaLibrary.sqlitedb
ENV_FILE ?= ../../../.env
SYSTEM_PROMPT ?= $(SHARED_DIR)/Resources/PromptSystem.txt

CPPFLAGS += -I$(SHARED_DIR) -I$(CJSON_DIR)
CFLAGS ?= -std=c99 -Wall -Wextra -Wconversion -Wsign-conversion -Wfloat-conversion
SQLITE_LIBS ?= -lsqlite3

RUNNER_SOURCES = \
	hill_climbing.c \
	$(SHARED_DIR)/strappy_client.c \
	$(SHARED_DIR)/strappy_config.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_core.c \
	$(SHARED_DIR)/strappy_db.c \
	$(SHARED_DIR)/strappy_prompt.c \
	$(SHARED_DIR)/strappy_responses.c \
	$(SHARED_DIR)/strappy_tools.c \
	$(CJSON_DIR)/cJSON.c

.DEFAULT_GOAL := build
.NOTPARALLEL: run
.PHONY: build verify-fixture run refresh-fixture clean

build: $(RUNNER)

$(BUILD_DIR):
	mkdir -p $@

$(RUNNER): $(RUNNER_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(RUNNER_SOURCES) $(SQLITE_LIBS) -lcurl

verify-fixture:
	@test -f "$(MEDIA_DB)" || { \
		echo "Missing private fixture: $(MEDIA_DB)" >&2; \
		echo "Run: make refresh-fixture" >&2; \
		exit 2; \
	}
	@sqlite3 -readonly "$(MEDIA_DB)" "PRAGMA quick_check;" | \
		grep -qx ok || { echo "MediaLibrary fixture failed quick_check" >&2; exit 2; }
	@git check-ignore -q "$(MEDIA_DB)" || { \
		echo "Refusing to use a MediaLibrary fixture that is not ignored by git." >&2; \
		exit 2; \
	}

run: build verify-fixture
	@test "$(CONFIRM_LIVE)" = "yes" || { \
		echo "Live OpenRouter calls are manual only." >&2; \
		echo "Run: make run CONFIRM_LIVE=yes" >&2; \
		exit 2; \
	}
	python3 run_hill_climbing.py \
		--runner "$(RUNNER)" \
		--media-db "$(MEDIA_DB)" \
		--env-file "$(ENV_FILE)" \
		--system-prompt "$(SYSTEM_PROMPT)" \
		$(if $(MODEL),--model "$(MODEL)",)

# This copies the complete WAL set. It is never a dependency of build or run.
refresh-fixture:
	mkdir -p "$(dir $(MEDIA_DB))"
	rm -f "$(MEDIA_DB)" "$(MEDIA_DB)-wal" "$(MEDIA_DB)-shm"
	scp -T -p -o BatchMode=yes \
		'gomadango:/private/var/mobile/Media/iTunes_Control/iTunes/MediaLibrary.sqlitedb*' \
		"$(dir $(MEDIA_DB))"
	$(MAKE) verify-fixture MEDIA_DB="$(MEDIA_DB)"

clean:
	rm -rf "$(BUILD_DIR)" __pycache__
