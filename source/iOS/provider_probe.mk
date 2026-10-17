# Native BulletinBoard provider experiment. Never part of production packages.
ALTIVEC_ROOT ?= /altivec
include $(ALTIVEC_ROOT)/make/entry.mk
ifeq ($(_altivec_relocated),)
PROJECT_ROOT := $(abspath ../..)
PRODUCTS = strappy-provider-probe StrappyProviderProbe
PACKAGE_PRODUCTS = strappy-provider-probe
PROBE_SOURCE = ../daemon/tests/provider_probe
PROBE_BUNDLE = System/Library/BulletinBoardPlugins/StrappyProviderProbe.bundle
strappy-provider-probe_TYPE = executable
strappy-provider-probe_SOURCES = $(PROBE_SOURCE)/sender.c
strappy-provider-probe_LDFLAGS = -framework CoreFoundation
strappy-provider-probe_SIGN = 1
strappy-provider-probe_PACKAGES = deb-rootful
strappy-provider-probe_PACKAGE_ID = com.altivecintelligence.strappy.provider-probe
strappy-provider-probe_PACKAGE_ARCH = iphoneos-arm
strappy-provider-probe_VERSION = 0.1.0
strappy-provider-probe_INSTALL = StrappyProviderProbe
strappy-provider-probe_PACKAGE_FILES = $(PROBE_SOURCE)/Info.plist=$(PROBE_BUNDLE)/Info.plist
StrappyProviderProbe_TYPE = executable
StrappyProviderProbe_SOURCES = $(PROBE_SOURCE)/XPProviderProbe.m
StrappyProviderProbe_LDFLAGS = -bundle -framework Foundation -framework CoreFoundation -lobjc
StrappyProviderProbe_SIGN = 1
StrappyProviderProbe_INSTALL_PATH = $(PROBE_BUNDLE)/StrappyProviderProbe
include $(ALTIVEC_ROOT)/altivec_common_phone.mk
endif
