# Run from source/iOS after the normal platform build.
STRAPPY_PHONE_INTERNAL_BUILD = 1
include Makefile
.DEFAULT_GOAL := webview-device-probe
.PHONY: webview-device-probe
webview-device-probe: $(BUILD_DIR)/webview-device-probe
$(BUILD_DIR)/webview-device-probe: ../linux/webview_device_probe.m
	$(CLANG) $(IOS_ARCH_FLAGS) $(IOS_FLAGS) $< $(IOS_FRAMEWORKS) -o $@
	$(PHONE_LDID) -S $@
