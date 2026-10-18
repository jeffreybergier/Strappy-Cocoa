# Removable badge-only experiment. Does not replace any production file.
ALTIVEC_ROOT ?= /altivec
include $(ALTIVEC_ROOT)/make/entry.mk
ifeq ($(_altivec_relocated),)
PROJECT_ROOT := $(abspath ../..)
PRODUCTS = strappy-badge-probe StrappyBadgeProbe
PACKAGE_PRODUCTS = strappy-badge-probe
PROBE_SOURCE = ../daemon/tests/badge_probe
PROBE_INSTALL = Library/MobileSubstrate/DynamicLibraries/StrappyBadgeProbe
strappy-badge-probe_TYPE = executable
strappy-badge-probe_SOURCES = $(PROBE_SOURCE)/sender.c
strappy-badge-probe_LDFLAGS = -framework CoreFoundation
strappy-badge-probe_SIGN = 1
strappy-badge-probe_PACKAGES = deb-rootful
strappy-badge-probe_PACKAGE_ID = com.altivecintelligence.strappy.badge-probe
strappy-badge-probe_PACKAGE_ARCH = iphoneos-arm
strappy-badge-probe_VERSION = 0.1.0
strappy-badge-probe_INSTALL = StrappyBadgeProbe
strappy-badge-probe_PACKAGE_FILES = $(PROBE_SOURCE)/StrappyBadgeProbe.plist=$(PROBE_INSTALL).plist
StrappyBadgeProbe_TYPE = executable
StrappyBadgeProbe_SOURCES = $(PROBE_SOURCE)/XPBadgeProbe.m
StrappyBadgeProbe_LDFLAGS = -dynamiclib -install_name /$(PROBE_INSTALL).dylib -framework Foundation -framework CoreFoundation -lobjc
StrappyBadgeProbe_SIGN = 1
StrappyBadgeProbe_INSTALL_PATH = $(PROBE_INSTALL).dylib
include $(ALTIVEC_ROOT)/altivec_common_phone.mk
endif
