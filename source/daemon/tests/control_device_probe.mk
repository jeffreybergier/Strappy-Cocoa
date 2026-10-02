# Run from source/iOS or source/macOS, matching the native application objects.
STRAPPY_PHONE_INTERNAL_BUILD = 1
include Makefile
.DEFAULT_GOAL := control-probe
.PHONY: control-probe
ifeq ($(notdir $(CURDIR)),iOS)
CONTROL_PROBE_OBJECTS = $(filter-out $(INT_DIR)/main.o,$(APP_SOURCE_OBJS) $(APP_EXTRA_OBJS))
control-probe: $(BUILD_DIR)/control-probe
$(INT_DIR)/control-probe.o: ../daemon/tests/control_device_probe.c
	$(CLANG) $(IOS_ARCH_FLAGS) $(IOS_FLAGS) -c $< -o $@
$(BUILD_DIR)/control-probe: $(INT_DIR)/control-probe.o $(CONTROL_PROBE_OBJECTS)
	$(CLANG) $(IOS_ARCH_FLAGS) $(IOS_FLAGS) $^ $(IOS_FRAMEWORKS) $(LIBS_IPHONE) -o $@
	$(PHONE_LDID) -S $@
else
CONTROL_PROBE_OBJECTS = $(filter-out $(INT_DIR)/ppc/main.o,$(PPC_OBJS))
control-probe: $(BUILD_DIR)/control-probe-ppc
$(INT_DIR)/control-probe-ppc.o: ../daemon/tests/control_device_probe.c
	MACOSX_DEPLOYMENT_TARGET=$(MAC_MIN_OLD) $(COMPILER_PPC) $(MAC_FLAGS) $(LEGACY_GCC_FLAGS) -arch ppc -isysroot $(SDK_MAC_OLD_PATH) -c $< -o $@
$(BUILD_DIR)/control-probe-ppc: $(INT_DIR)/control-probe-ppc.o $(CONTROL_PROBE_OBJECTS)
	MACOSX_DEPLOYMENT_TARGET=$(MAC_MIN_OLD) $(COMPILER_PPC) -arch ppc -isysroot $(SDK_MAC_OLD_PATH) $^ $(MAC_LIBS) -lgcc_s.10.4 -o $@
endif
