# Run from source/macOS. The same offline fixture also checks the Tiger ABI.
include Makefile
.DEFAULT_GOAL := async-ui-probe
ASYNC_PROBE_OBJECTS = $(filter-out $(INT_DIR)/ppc/main.o,$(PPC_OBJS))
.PHONY: async-ui-probe
async-ui-probe: $(BUILD_DIR)/async-ui-probe-ppc
$(INT_DIR)/async-ui-probe-ppc.o: ../daemon/tests/async_ui_device_probe.m
	MACOSX_DEPLOYMENT_TARGET=$(MAC_MIN_OLD) $(COMPILER_PPC) $(MAC_FLAGS) $(LEGACY_GCC_FLAGS) -arch ppc -isysroot $(SDK_MAC_OLD_PATH) -c $< -o $@
$(BUILD_DIR)/async-ui-probe-ppc: $(INT_DIR)/async-ui-probe-ppc.o $(ASYNC_PROBE_OBJECTS)
	MACOSX_DEPLOYMENT_TARGET=$(MAC_MIN_OLD) $(COMPILER_PPC) -arch ppc -isysroot $(SDK_MAC_OLD_PATH) $^ $(MAC_LIBS) -lgcc_s.10.4 -o $@
