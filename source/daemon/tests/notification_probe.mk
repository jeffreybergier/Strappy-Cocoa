# Same native delegate as the IPA sandbox experiment, isolated product names.
ALTIVEC_ROOT ?= /altivec
include $(ALTIVEC_ROOT)/make/entry.mk
ifeq ($(_altivec_relocated),)
PROJECT_ROOT := $(abspath $(dir $(firstword $(MAKEFILE_LIST)))/../../..)
PRODUCTS = $(PROBE_PRODUCT)
GROUP_probe_SOURCES = sandbox_probe.m
GROUP_probe_MEMORY = arc
GROUP_ipc_SOURCES = ../../shared/strappy_ipc.c \
  ../../shared/XPFoundation/strappy_ipc_platform.c
$(PROBE_PRODUCT)_TYPE = app
$(PROBE_PRODUCT)_GROUPS = probe ipc
$(PROBE_PRODUCT)_PLIST = $(PROBE_INPUT)/Info.plist
$(PROBE_PRODUCT)_ENTITLEMENTS = $(PROBE_INPUT)/Entitlements.plist
$(PROBE_PRODUCT)_SANDBOX = unsandboxed
$(PROBE_PRODUCT)_FLAGS = -I../../shared
$(PROBE_PRODUCT)_CORE = static
$(PROBE_PRODUCT)_COCOA = static
$(PROBE_PRODUCT)_SQLITE = 1
$(PROBE_PRODUCT)_LDFLAGS = -framework UIKit -framework Foundation \
  -framework CoreGraphics
$(PROBE_PRODUCT)_RESOURCES = $(wildcard ../../iOS/Resources/*.png)
$(PROBE_PRODUCT)_PACKAGES = $(PROBE_FORMAT)
$(PROBE_PRODUCT)_PACKAGE_ID = $(PROBE_ID)
$(PROBE_PRODUCT)_PACKAGE_ARCH = all
$(PROBE_PRODUCT)_VERSION = $(PROBE_VERSION)
include $(ALTIVEC_ROOT)/altivec_common_phone.mk
endif
