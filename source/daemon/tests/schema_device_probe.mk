# Compatibility entry: invoke from source/iOS or source/macOS.
.DEFAULT_GOAL := release
.PHONY: release schema-probe schema-probes
release schema-probe schema-probes:
	+$(MAKE) -f ../daemon/tests/native.mk PLATFORM=$(notdir $(CURDIR)) TEST_PRODUCTS="schema-probe schema-reader-probe" release
