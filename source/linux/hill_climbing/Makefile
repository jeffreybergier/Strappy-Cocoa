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
DEVICE_HOST ?= gomadango
DEVICE_FIXTURE_DIR ?= private/gomadango
DATABASE_MANIFEST ?= $(DEVICE_FIXTURE_DIR)/databases.json
MEMBER_ROSTER ?= $(DEVICE_FIXTURE_DIR)/member_roster.json
SYNC_FIXTURES := sync_device_fixtures.py
ENV_FILE ?= ../../../.env
SYSTEM_PROMPT ?= $(SHARED_DIR)/Resources/PromptSystemDatabase.txt

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
.PHONY: build test verify-fixture run refresh-fixture clean

build: $(RUNNER)

test:
	python3 -m unittest -v test_evaluate.py

$(BUILD_DIR):
	mkdir -p $@

$(RUNNER): $(RUNNER_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(RUNNER_SOURCES) $(SQLITE_LIBS) -lcurl

verify-fixture: build
	@test -f "$(DATABASE_MANIFEST)" || { \
		echo "Missing private fixture manifest: $(DATABASE_MANIFEST)" >&2; \
		echo "Run: make refresh-fixture" >&2; \
		exit 2; \
	}
	@python3 "$(SYNC_FIXTURES)" \
		--fixture-dir "$(DEVICE_FIXTURE_DIR)" --verify-only \
		--runner "$(RUNNER)"
	@git check-ignore -q "$(DATABASE_MANIFEST)" || { \
		echo "Refusing to use a database manifest that is not ignored by git." >&2; \
		exit 2; \
	}

run: build verify-fixture
	@test -f "$(MEMBER_ROSTER)" || { \
		echo "Missing prefetched member roster: $(MEMBER_ROSTER)" >&2; \
		exit 2; \
	}
	@test "$(CONFIRM_LIVE)" = "yes" || { \
		echo "Live OpenRouter calls are manual only." >&2; \
		echo "Run: make run CONFIRM_LIVE=yes" >&2; \
		exit 2; \
	}
	python3 run_hill_climbing.py \
		--runner "$(RUNNER)" \
		--database-manifest "$(DATABASE_MANIFEST)" \
		--member-roster "$(MEMBER_ROSTER)" \
		--env-file "$(ENV_FILE)" \
		--system-prompt "$(SYSTEM_PROMPT)" \
		$(if $(MODEL),--model "$(MODEL)",)

# This copies every Strappy-approved database and its SQLite sidecars. It is
# never a dependency of build or run.
refresh-fixture:
	python3 "$(SYNC_FIXTURES)" \
		--host "$(DEVICE_HOST)" --fixture-dir "$(DEVICE_FIXTURE_DIR)"
	$(MAKE) verify-fixture \
		DEVICE_FIXTURE_DIR="$(DEVICE_FIXTURE_DIR)" \
		DATABASE_MANIFEST="$(DATABASE_MANIFEST)"

clean:
	rm -rf "$(BUILD_DIR)" __pycache__
