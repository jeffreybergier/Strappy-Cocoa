# Linux-only shared-core harnesses.
#
# These are developer smoke tests for Strappy's portable C code. They compile
# the shared sources directly against the host sqlite3 library and the cJSON
# source vendored in the Altivec tree.

CC := clang
CJSON_DIR ?= /altivec/libs/core/third_party/cJSON
SHARED_DIR := ../shared

CPPFLAGS += -I$(SHARED_DIR) -I$(CJSON_DIR)
CFLAGS ?= -std=c99 -Wall -Wextra -Wconversion -Wsign-conversion -Wfloat-conversion
SQLITE_LIBS ?= -lsqlite3

DATABASE_QUERY_HARNESS_SOURCES = \
	database_query_harness.c \
	$(SHARED_DIR)/strappy_cocoa.c \
	$(SHARED_DIR)/strappy_tools.c \
	$(SHARED_DIR)/strappy_db.c \
	$(SHARED_DIR)/strappy_core.c \
	$(CJSON_DIR)/cJSON.c

.PHONY: test clean

test: database_query_harness
	./database_query_harness

database_query_harness: $(DATABASE_QUERY_HARNESS_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(DATABASE_QUERY_HARNESS_SOURCES) $(SQLITE_LIBS)

clean:
	rm -f database_query_harness
