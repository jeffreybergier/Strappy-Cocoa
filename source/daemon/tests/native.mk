# Offline native schema/auth probes; use PLATFORM=macOS or PLATFORM=iOS.
ALTIVEC_ROOT ?= /altivec
include $(ALTIVEC_ROOT)/make/entry.mk
ifeq ($(_altivec_relocated),)
PROJECT_ROOT := $(abspath $(dir $(firstword $(MAKEFILE_LIST)))/../../..)
STRAPPY_SOURCE_ROOT := $(PROJECT_ROOT)/source
include ../store.mk
PLATFORM ?= iOS
TEST_PRODUCTS ?= auth-probe schema-probe schema-reader-probe
PRODUCTS = $(TEST_PRODUCTS)
OUTPUT_DIR = $(BUILD_ROOT)/tests/$(PLATFORM)/daemon/$(CONFIG)
PACKAGE_PRODUCTS =
sidebar-probe_TYPE = executable
sidebar-probe_SOURCES = sidebar_harness.c $(STRAPPY_STORE_SOURCES)
sidebar-probe_FLAGS = $(auth-probe_FLAGS)
sidebar-probe_CORE = static
sidebar-probe_SIGN = $(auth-probe_SIGN)
sidebar-probe_LDFLAGS = $(auth-probe_LDFLAGS)
auth-probe_TYPE = executable
auth-probe_SOURCES = auth_harness.c ../strappy_auth.c $(STRAPPY_STORE_SOURCES)
auth-probe_FLAGS = -DSTRAPPY_DB_WRITER=1 -I../ -I../../shared
auth-probe_CORE = static
auth-probe_SIGN = $(if $(filter iOS,$(PLATFORM)),1,0)
auth-probe_LDFLAGS = -framework Foundation -framework CoreFoundation -framework SystemConfiguration -framework Security -lobjc
schema-probe_TYPE = executable
schema-probe_SOURCES = schema_harness.c $(STRAPPY_STORE_SOURCES)
schema-probe_FLAGS = $(auth-probe_FLAGS)
schema-probe_CORE = static
schema-probe_SIGN = $(auth-probe_SIGN)
schema-probe_LDFLAGS = $(auth-probe_LDFLAGS)
schema-reader-probe_TYPE = executable
schema-reader-probe_SOURCES = store_reader_harness.c $(STRAPPY_STORE_SOURCES)
schema-reader-probe_FLAGS = -DSTRAPPY_DB_UI=1 -I../ -I../../shared
schema-reader-probe_CORE = static
schema-reader-probe_SIGN = $(auth-probe_SIGN)
schema-reader-probe_LDFLAGS = $(auth-probe_LDFLAGS)
delete-probe_TYPE = executable
delete-probe_SOURCES = session_delete_harness.c $(STRAPPY_STORE_SOURCES)
delete-probe_FLAGS = $(auth-probe_FLAGS)
delete-probe_CORE = static
delete-probe_SIGN = $(auth-probe_SIGN)
delete-probe_LDFLAGS = $(auth-probe_LDFLAGS)
startup-probe_TYPE = executable
startup-probe_SOURCES = mac_startup_test.m ../../macOS/XPAppKit.m
startup-probe_FLAGS = -I../../macOS -I../../shared
startup-probe_FLAGS_x86_64 = -fblocks
startup-probe_FLAGS_arm64 = -fblocks
startup-probe_COCOA = static
startup-probe_LDFLAGS = -framework AppKit -lobjc
account-endpoint-probe_TYPE = executable
account-endpoint-probe_SOURCES = account_endpoint_device_probe.c ../../shared/strappy_ipc.c ../../shared/XPFoundation/strappy_ipc_platform.c
account-endpoint-probe_FLAGS = -I../../shared
account-endpoint-probe_CORE = static
account-endpoint-probe_SIGN = $(auth-probe_SIGN)
account-endpoint-probe_LDFLAGS = $(auth-probe_LDFLAGS)
catalog-refresh-probe_TYPE = executable
catalog-refresh-probe_SOURCES = catalog_refresh_device_probe.c ../../shared/strappy_ipc.c ../../shared/XPFoundation/strappy_ipc_platform.c
catalog-refresh-probe_FLAGS = -I../../shared
catalog-refresh-probe_CORE = static
catalog-refresh-probe_SIGN = $(auth-probe_SIGN)
catalog-refresh-probe_LDFLAGS = $(auth-probe_LDFLAGS)
attention-probe_TYPE = executable
attention-probe_SOURCES = attention_harness.c ../strappy_attention.c ../strappy_notifications.c ../../shared/XPFoundation/strappy_notification_platform.c ../../shared/strappy_ipc.c ../../shared/XPFoundation/strappy_ipc_platform.c ../strappy_ipc_server.c $(STRAPPY_STORE_SOURCES)
attention-probe_FLAGS = $(auth-probe_FLAGS) -DSTRAPPY_ENABLE_LOOPBACK_HTTP_TESTS=1
attention-probe_CORE = static
attention-probe_SIGN = $(auth-probe_SIGN)
attention-probe_LDFLAGS = $(auth-probe_LDFLAGS)
provider-policy-probe_TYPE = executable
provider-policy-probe_SOURCES = notification_provider_test.m
provider-policy-probe_FLAGS = -DXP_PROVIDER_DIAGNOSTIC=0
provider-policy-probe_SIGN = $(auth-probe_SIGN)
provider-policy-probe_LDFLAGS = -framework Foundation -framework CoreFoundation -framework AudioToolbox -lobjc
provider-badge-sender_TYPE = executable
provider-badge-sender_SOURCES = notification_provider_device/badge_sender.c
provider-badge-sender_SIGN = $(auth-probe_SIGN)
include $(ALTIVEC_ROOT)/altivec_common_$(if $(filter iOS,$(PLATFORM)),phone,mac).mk
_package: $(foreach p,$(TEST_PRODUCTS),_product-$(p))
endif
