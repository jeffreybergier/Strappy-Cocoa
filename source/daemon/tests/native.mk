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
include $(ALTIVEC_ROOT)/altivec_common_$(if $(filter iOS,$(PLATFORM)),phone,mac).mk
_package: $(foreach p,$(TEST_PRODUCTS),_product-$(p))
endif
