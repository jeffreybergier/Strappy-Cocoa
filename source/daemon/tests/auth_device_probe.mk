# Native authorization-manager regression; synthetic provider/credential boundaries.
STRAPPY_PHONE_INTERNAL_BUILD = 1
include Makefile
.DEFAULT_GOAL := auth-probe
.PHONY: auth-probe
AUTH_PROBE_SOURCES = ../daemon/tests/auth_harness.c ../daemon/strappy_auth.c $(STRAPPY_STORE_SOURCES)
ifeq ($(notdir $(CURDIR)),iOS)
auth-probe: $(BUILD_DIR)/auth-probe
$(BUILD_DIR)/auth-probe: $(AUTH_PROBE_SOURCES)
	$(CLANG) $(IOS_ARCH_FLAGS) $(IOS_FLAGS) -I../daemon -USTRAPPY_DB_UI -DSTRAPPY_DB_WRITER=1 $^ $(ALTIVECCORE_DIR)/lib/libAltivecCore.a -framework Foundation -o $@
	$(PHONE_LDID) -S $@
else
auth-probe: $(BUILD_DIR)/auth-probe-ppc
$(BUILD_DIR)/auth-probe-ppc: $(AUTH_PROBE_SOURCES)
	MACOSX_DEPLOYMENT_TARGET=$(MAC_MIN_OLD) $(COMPILER_PPC) $(MAC_FLAGS) $(LEGACY_GCC_FLAGS) -I../daemon -USTRAPPY_DB_UI -DSTRAPPY_DB_WRITER=1 -arch ppc -isysroot $(SDK_MAC_OLD_PATH) $^ $(ALTIVECCORE_DIR)/lib/libAltivecCore.a -framework Foundation -lgcc_s.10.4 -o $@
endif
