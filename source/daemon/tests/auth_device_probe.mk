# Compatibility entry: invoke from source/iOS or source/macOS.
.DEFAULT_GOAL := release
.PHONY: release auth-probe auth-probes
release auth-probe auth-probes:
	+$(MAKE) -f ../daemon/tests/native.mk PLATFORM=$(notdir $(CURDIR)) TEST_PRODUCTS="auth-probe" release
