# Run the matching build operation for both Apple platforms.
.DEFAULT_GOAL := release

.PHONY: clean debug release analyze test

clean debug release analyze:
	+$(MAKE) -C iOS $@
	+$(MAKE) -C macOS $@

test:
	+$(MAKE) -C linux test
