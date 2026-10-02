# Compatibility entry point; harness implementation lives with the daemon.
.DEFAULT_GOAL := test
.PHONY: test
%:
	+$(MAKE) -C ../daemon/tests $@
test:
	+$(MAKE) -C ../daemon/tests test
