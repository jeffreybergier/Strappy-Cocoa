# Build the production Cocoa bridge with a test-only acceptance delay.
PLATFORM ?= $(notdir $(CURDIR))
MAINTENANCE_PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../..)
.DEFAULT_GOAL := maintenance-probe
.PHONY: maintenance-probe debug release
maintenance-probe: debug
debug release:
	+$(MAKE) -C $(MAINTENANCE_PROJECT_ROOT)/source/$(PLATFORM) maintenance-probe CONFIG=$@ \
	  PRODUCTS=maintenance-probe PACKAGE_PRODUCTS= \
	  maintenance-probe_TYPE=executable \
	  'maintenance-probe_GROUPS=maintenance_common maintenance_bridge' \
	  'GROUP_maintenance_common_SOURCES=$$(filter-out StrappySession.m FileScanner.m,$$(GROUP_shared_SOURCES)) ../daemon/tests/maintenance_device_probe.m' \
	  'GROUP_maintenance_bridge_SOURCES=../shared/StrappySession.m ../shared/FileScanner.m' \
	  GROUP_maintenance_bridge_FLAGS=-Dstrappy_store_client_mutation=strappy_test_mutation \
	  'BUILD_FLAGS=-DSTRAPPY_DB_UI=1 -I../shared' \
	  maintenance-probe_CORE=static maintenance-probe_COCOA=static maintenance-probe_SQLITE=1 \
	  'maintenance-probe_GENERATED=$$(FONTAWESOME_GENERATED)' \
	  maintenance-probe_SIGN=$(if $(filter iOS,$(PLATFORM)),1,0) \
	  'maintenance-probe_LDFLAGS=-framework Foundation -framework CoreFoundation -framework SystemConfiguration -framework Security -lobjc'
