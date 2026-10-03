# Run from source/iOS or source/macOS after the normal platform build:
# make -f ../daemon/tests/session_options_device_probe.mk session-options-probe
# Transport replacements apply only to the test copy of the Cocoa bridge.
STRAPPY_PHONE_INTERNAL_BUILD = 1
include Makefile
.DEFAULT_GOAL := session-options-probe
ifeq ($(notdir $(CURDIR)),iOS)
OPTIONS_PROBE_DIR = $(INT_DIR)/options-probe
OPTIONS_PROBE_OBJECTS = $(filter-out $(INT_DIR)/main.o $(INT_DIR)/StrappySession.o,$(APP_SOURCE_OBJS) $(APP_EXTRA_OBJS))

.PHONY: session-options-probe
session-options-probe: $(BUILD_DIR)/session-options-probe

$(OPTIONS_PROBE_DIR):
	mkdir -p $@

$(OPTIONS_PROBE_DIR)/StrappySession.o: ../shared/StrappySession.m ../shared/StrappySession.h | $(OPTIONS_PROBE_DIR)
	$(CLANG) $(IOS_ARCH_FLAGS) $(IOS_FLAGS) -Dstrappy_store_client_mutation=strappy_test_mutation -Dstrappy_store_client_command=strappy_test_command -c $< -o $@

$(OPTIONS_PROBE_DIR)/main.o: ../daemon/tests/session_options_device_probe.m | $(OPTIONS_PROBE_DIR)
	$(CLANG) $(IOS_ARCH_FLAGS) $(IOS_FLAGS) -c $< -o $@

$(BUILD_DIR)/session-options-probe: $(OPTIONS_PROBE_DIR)/main.o $(OPTIONS_PROBE_DIR)/StrappySession.o $(OPTIONS_PROBE_OBJECTS)
	$(CLANG) $(IOS_ARCH_FLAGS) $(IOS_FLAGS) $^ $(IOS_FRAMEWORKS) $(LIBS_IPHONE) -o $@
	$(PHONE_LDID) -S $@

else
OPTIONS_PROBE_DIR = $(INT_DIR)/options-probe-ppc
OPTIONS_PROBE_OBJECTS = $(filter-out $(INT_DIR)/ppc/main.o $(INT_DIR)/ppc/StrappySession.o,$(PPC_OBJS))
.PHONY: session-options-probe
session-options-probe: $(BUILD_DIR)/session-options-probe-ppc
$(OPTIONS_PROBE_DIR):
	mkdir -p $@
$(OPTIONS_PROBE_DIR)/StrappySession.o: ../shared/StrappySession.m ../shared/StrappySession.h | $(OPTIONS_PROBE_DIR)
	MACOSX_DEPLOYMENT_TARGET=$(MAC_MIN_OLD) $(COMPILER_PPC) $(MAC_FLAGS) $(LEGACY_GCC_FLAGS) -arch ppc -isysroot $(SDK_MAC_OLD_PATH) -Dstrappy_store_client_mutation=strappy_test_mutation -Dstrappy_store_client_command=strappy_test_command -c $< -o $@
$(OPTIONS_PROBE_DIR)/main.o: ../daemon/tests/session_options_device_probe.m | $(OPTIONS_PROBE_DIR)
	MACOSX_DEPLOYMENT_TARGET=$(MAC_MIN_OLD) $(COMPILER_PPC) $(MAC_FLAGS) $(LEGACY_GCC_FLAGS) -arch ppc -isysroot $(SDK_MAC_OLD_PATH) -c $< -o $@
$(BUILD_DIR)/session-options-probe-ppc: $(OPTIONS_PROBE_DIR)/main.o $(OPTIONS_PROBE_DIR)/StrappySession.o $(OPTIONS_PROBE_OBJECTS)
	MACOSX_DEPLOYMENT_TARGET=$(MAC_MIN_OLD) $(COMPILER_PPC) -arch ppc -isysroot $(SDK_MAC_OLD_PATH) $^ $(MAC_LIBS) -lgcc_s.10.4 -o $@
endif
