# Explicit removable device-test package. No production package dependency.
ALTIVEC_ROOT ?= /altivec
include $(ALTIVEC_ROOT)/make/entry.mk
ifeq ($(_altivec_relocated),)
PROJECT_ROOT := $(abspath ../..)
PRODUCTS = strappy-provider-test StrappyNotificationProvider
PACKAGE_PRODUCTS = strappy-provider-test
PROVIDER_DIR = notifications/provider
PROVIDER_BUNDLE = System/Library/BulletinBoardPlugins/StrappyNotificationProvider.bundle
strappy-provider-test_TYPE = executable
strappy-provider-test_SOURCES = ../daemon/tests/notification_provider_device/sender.c
strappy-provider-test_SIGN = 1
strappy-provider-test_PACKAGES = deb-rootful
strappy-provider-test_PACKAGE_ID = com.altivecintelligence.strappy.notification-provider-test
strappy-provider-test_PACKAGE_ARCH = iphoneos-arm
strappy-provider-test_VERSION = 0.1.5
strappy-provider-test_INSTALL = StrappyNotificationProvider
strappy-provider-test_PACKAGE_FILES = $(PROVIDER_DIR)/Info.plist=$(PROVIDER_BUNDLE)/Info.plist
StrappyNotificationProvider_TYPE = executable
StrappyNotificationProvider_FLAGS = -DXP_PROVIDER_DIAGNOSTIC=1
StrappyNotificationProvider_SOURCES = $(PROVIDER_DIR)/XPStrappyNotificationProvider.m
StrappyNotificationProvider_LDFLAGS = -bundle -framework Foundation -framework CoreFoundation -framework AudioToolbox -lobjc
StrappyNotificationProvider_SIGN = 1
StrappyNotificationProvider_INSTALL_PATH = $(PROVIDER_BUNDLE)/StrappyNotificationProvider
include $(ALTIVEC_ROOT)/altivec_common_phone.mk
endif
