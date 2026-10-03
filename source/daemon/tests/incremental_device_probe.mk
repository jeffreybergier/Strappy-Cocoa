# Run from source/macOS after the normal build; offline PPC fixture.
include Makefile
.DEFAULT_GOAL := incremental-probe
INCREMENTAL_PROBE_OBJECTS = $(filter-out $(INT_DIR)/ppc/main.o,$(PPC_OBJS))
.PHONY: incremental-probe
incremental-probe: $(BUILD_DIR)/incremental-probe-ppc
$(INT_DIR)/incremental-probe-ppc.o: ../daemon/tests/incremental_device_probe.m
	MACOSX_DEPLOYMENT_TARGET=$(MAC_MIN_OLD) $(COMPILER_PPC) $(MAC_FLAGS) $(LEGACY_GCC_FLAGS) -arch ppc -isysroot $(SDK_MAC_OLD_PATH) -c $< -o $@
$(INT_DIR)/incremental-server-ppc.o: ../daemon/strappy_ipc_server.c
	MACOSX_DEPLOYMENT_TARGET=$(MAC_MIN_OLD) $(COMPILER_PPC) $(MAC_FLAGS) $(LEGACY_GCC_FLAGS) -arch ppc -isysroot $(SDK_MAC_OLD_PATH) -c $< -o $@
$(BUILD_DIR)/incremental-probe-ppc: $(INT_DIR)/incremental-probe-ppc.o $(INT_DIR)/incremental-server-ppc.o $(INCREMENTAL_PROBE_OBJECTS)
	MACOSX_DEPLOYMENT_TARGET=$(MAC_MIN_OLD) $(COMPILER_PPC) -arch ppc -isysroot $(SDK_MAC_OLD_PATH) $^ $(MAC_LIBS) -lgcc_s.10.4 -o $@
