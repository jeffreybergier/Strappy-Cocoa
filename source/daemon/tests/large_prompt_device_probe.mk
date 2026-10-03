# Run from source/iOS or source/macOS after the normal platform build.
STRAPPY_PHONE_INTERNAL_BUILD = 1
include Makefile
.DEFAULT_GOAL := large-prompt-probe
LARGE_PROMPT_SOURCES = ../daemon/tests/large_prompt_device_probe.c ../shared/strappy_ipc.c ../shared/XPFoundation/strappy_ipc_platform.c
.PHONY: large-prompt-probe
ifeq ($(notdir $(CURDIR)),iOS)
large-prompt-probe: $(BUILD_DIR)/large-prompt-probe
$(BUILD_DIR)/large-prompt-probe: $(LARGE_PROMPT_SOURCES) ../shared/strappy_ipc.h
	$(CLANG) $(IOS_ARCH_FLAGS) $(IOS_FLAGS) -I../shared $(LARGE_PROMPT_SOURCES) $(ALTIVECCORE_DIR)/lib/libAltivecCore.a -framework Foundation -o $@
	$(PHONE_LDID) -S $@
else
large-prompt-probe: $(BUILD_DIR)/large-prompt-probe-ppc
$(BUILD_DIR)/large-prompt-probe-ppc: $(LARGE_PROMPT_SOURCES) ../shared/strappy_ipc.h
	MACOSX_DEPLOYMENT_TARGET=$(MAC_MIN_OLD) $(COMPILER_PPC) $(STRAPPY_DAEMON_FLAGS) $(LEGACY_GCC_FLAGS) -arch ppc -isysroot $(SDK_MAC_OLD_PATH) $(LARGE_PROMPT_SOURCES) $(ALTIVECCORE_DIR)/lib/libAltivecCore.a -framework Foundation -lgcc_s.10.4 -o $@
endif
