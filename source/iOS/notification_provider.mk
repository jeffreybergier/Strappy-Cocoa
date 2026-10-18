# Offline candidate; deliberately excluded from the production app package.
ALTIVEC_ROOT ?= /altivec
include $(ALTIVEC_ROOT)/make/entry.mk
ifeq ($(_altivec_relocated),)
PROJECT_ROOT := $(abspath ../..)
PRODUCTS = StrappyNotificationProvider
PROVIDER_DIR = notifications/provider
PROVIDER_BUNDLE = StrappyNotificationProvider.bundle
StrappyNotificationProvider_TYPE = executable
StrappyNotificationProvider_SOURCES = $(PROVIDER_DIR)/XPStrappyNotificationProvider.m
StrappyNotificationProvider_LDFLAGS = -bundle -framework Foundation -framework CoreFoundation -framework AudioToolbox -lobjc
StrappyNotificationProvider_SIGN = 1
StrappyNotificationProvider_PACKAGE_MAIN_PATH = $(PROVIDER_BUNDLE)/StrappyNotificationProvider
StrappyNotificationProvider_PACKAGE_RESOURCES = $(PROVIDER_DIR)/Info.plist=$(PROVIDER_BUNDLE)/Info.plist
StrappyNotificationProvider_PACKAGES = zip
StrappyNotificationProvider_VERSION = 0.1.3
include $(ALTIVEC_ROOT)/altivec_common_phone.mk
endif
