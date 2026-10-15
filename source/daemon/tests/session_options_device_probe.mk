# Compatibility entry from source/iOS or source/macOS; common product rules
# compile the real shared UI code with only the bridge transports replaced.
PLATFORM ?= $(notdir $(CURDIR))
OPTIONS_PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../..)
.DEFAULT_GOAL := session-options-probe
.PHONY: session-options-probe debug release
session-options-probe: debug
debug release:
	+$(MAKE) -C $(OPTIONS_PROJECT_ROOT)/source/$(PLATFORM) options-probe CONFIG=$@ \
	  PRODUCTS=options-probe PACKAGE_PRODUCTS= \
	  options-probe_TYPE=executable \
	  'options-probe_GROUPS=options_common options_bridge' \
	  'GROUP_options_common_SOURCES=$$(filter-out StrappySession.m,$$(GROUP_shared_SOURCES)) ../daemon/tests/session_options_device_probe.m' \
	  GROUP_options_bridge_SOURCES=../shared/StrappySession.m \
	  'GROUP_options_bridge_FLAGS=-Dstrappy_store_client_credential=strappy_test_credential -Dstrappy_store_client_mutation=strappy_test_mutation -Dstrappy_store_client_command=strappy_test_command' \
	  'BUILD_FLAGS=-DSTRAPPY_DB_UI=1 -I../shared' \
	  options-probe_CORE=static options-probe_COCOA=static options-probe_SQLITE=1 \
	  'options-probe_GENERATED=$$(FONTAWESOME_GENERATED)' \
	  options-probe_SIGN=$(if $(filter iOS,$(PLATFORM)),1,0) \
	  'options-probe_LDFLAGS=-framework Foundation -framework CoreFoundation -framework SystemConfiguration -framework Security -lobjc'
