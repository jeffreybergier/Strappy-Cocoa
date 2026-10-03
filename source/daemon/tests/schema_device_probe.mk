# Run from source/iOS after the normal build. Same probes on armv7/arm64.
STRAPPY_PHONE_INTERNAL_BUILD = 1
include Makefile
.DEFAULT_GOAL := schema-probes
.PHONY: schema-probes
schema-probes: $(BUILD_DIR)/schema-probe $(BUILD_DIR)/schema-reader-probe
$(BUILD_DIR)/schema-probe: ../daemon/tests/schema_harness.c $(STRAPPY_STORE_SOURCES)
	$(CLANG) $(IOS_ARCH_FLAGS) $(IOS_FLAGS) -I../daemon -USTRAPPY_DB_UI -DSTRAPPY_DB_WRITER=1 $^ $(ALTIVECCORE_DIR)/lib/libAltivecCore.a -framework Foundation -o $@
	$(PHONE_LDID) -S $@
$(BUILD_DIR)/schema-reader-probe: ../daemon/tests/store_reader_harness.c $(STRAPPY_STORE_SOURCES)
	$(CLANG) $(IOS_ARCH_FLAGS) $(IOS_FLAGS) -I../daemon -USTRAPPY_DB_WRITER -DSTRAPPY_DB_UI=1 $^ $(ALTIVECCORE_DIR)/lib/libAltivecCore.a -framework Foundation -o $@
	$(PHONE_LDID) -S $@
