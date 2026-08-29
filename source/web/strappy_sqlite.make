override MAKEFILE := GNUmakefile
override dir.wasm := .
override dir.tmp := ./bld
override dir.dout := ./jswasm
STRAPPY_WEB_DIR := $(abspath $(dir $(filter %/strappy_sqlite.make,$(MAKEFILE_LIST))))
STRAPPY_SHARED_DIR := $(abspath $(STRAPPY_WEB_DIR)/../shared)
STRAPPY_EXPORTS := $(STRAPPY_WEB_DIR)/strappy_sqlite_exports.txt
STRAPPY_COMBINED_EXPORTS := $(dir.tmp)/EXPORTED_FUNCTIONS.strappy
STRAPPY_POST_JS := $(STRAPPY_WEB_DIR)/strappy_sqlite_post.js
STRAPPY_PRE_JS := $(STRAPPY_WEB_DIR)/strappy_sqlite_pre.js
STRAPPY_CJSON_DIR := $(abspath $(STRAPPY_WEB_DIR)/build-sqlite/cJSON-1.7.19)
STRAPPY_SOURCES := \
  $(STRAPPY_SHARED_DIR)/strappy_core.c \
  $(STRAPPY_SHARED_DIR)/strappy_provider.c \
  $(STRAPPY_SHARED_DIR)/strappy_config.c \
  $(STRAPPY_SHARED_DIR)/strappy_client.c \
  $(STRAPPY_SHARED_DIR)/strappy_client_web.c \
  $(STRAPPY_SHARED_DIR)/strappy_db_connection_web.c \
  $(STRAPPY_SHARED_DIR)/strappy_db.c \
  $(STRAPPY_SHARED_DIR)/strappy_db_catalog.c \
  $(STRAPPY_SHARED_DIR)/strappy_db_sessions.c \
  $(STRAPPY_CJSON_DIR)/cJSON.c \
  $(STRAPPY_WEB_DIR)/strappy_sse_web.c \
  $(STRAPPY_WEB_DIR)/strappy_web_client.c \
  $(STRAPPY_WEB_DIR)/strappy_web_database.c

$(STRAPPY_COMBINED_EXPORTS): $(EXPORTED_FUNCTIONS.api) $(STRAPPY_EXPORTS)
	@mkdir -p $(dir $@)
	@{ cat $(EXPORTED_FUNCTIONS.api); cat $(STRAPPY_EXPORTS); } | sort -u > $@

$(out.esm.js): $(STRAPPY_COMBINED_EXPORTS) $(STRAPPY_SOURCES) \
  $(STRAPPY_PRE_JS) $(STRAPPY_POST_JS)

override sqlite3-wasm.c.in += $(STRAPPY_SOURCES)
override cflags.common += -I$(STRAPPY_SHARED_DIR) -I$(STRAPPY_CJSON_DIR)
override emcc.jsflags := $(filter-out \
  -sWASM_BIGINT=% -sIMPORTED_MEMORY,$(emcc.jsflags))
override emcc.jsflags += \
  -sASYNCIFY=1 \
  -sEXPORTED_FUNCTIONS=@$(STRAPPY_COMBINED_EXPORTS) \
  -sEXPORTED_RUNTIME_METHODS=wasmMemory,ccall \
  --js-library $(STRAPPY_WEB_DIR)/strappy_client_fetch.js

override emcc.environment.esm = web,worker,node
override emcc.flags.esm += \
  --pre-js=$(STRAPPY_PRE_JS) \
  --post-js=$(STRAPPY_POST_JS)

# Binaryen 132 opportunistically emits the not-yet-widely-supported compact
# imports proposal when upstream's extra `--all-features` pass runs. The emcc
# optimization pass is sufficient here and preserves the browser-compatible
# core Wasm encoding.
override b.do.wasm-opt = true
