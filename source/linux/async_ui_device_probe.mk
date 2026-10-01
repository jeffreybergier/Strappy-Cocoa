# Run from source/iOS after the normal build.
STRAPPY_PHONE_INTERNAL_BUILD = 1
include Makefile
.DEFAULT_GOAL := async-ui-probe
ASYNC_PROBE_OBJECTS = $(filter-out $(INT_DIR)/main.o,$(APP_SOURCE_OBJS) $(APP_EXTRA_OBJS))
.PHONY: async-ui-probe
async-ui-probe: $(BUILD_DIR)/async-ui-probe
$(INT_DIR)/async-ui-probe.o: ../linux/async_ui_device_probe.m
	$(CLANG) $(IOS_ARCH_FLAGS) $(IOS_FLAGS) -c $< -o $@
$(BUILD_DIR)/async-ui-probe: $(INT_DIR)/async-ui-probe.o $(ASYNC_PROBE_OBJECTS)
	$(CLANG) $(IOS_ARCH_FLAGS) $(IOS_FLAGS) $^ $(IOS_FRAMEWORKS) $(LIBS_IPHONE) -o $@
	$(PHONE_LDID) -S $@
