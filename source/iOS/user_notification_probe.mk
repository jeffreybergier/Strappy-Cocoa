# Standalone foreground diagnostic; never included in production packages.
ALTIVEC_ROOT ?= /altivec
include $(ALTIVEC_ROOT)/make/entry.mk
ifeq ($(_altivec_relocated),)
PROJECT_ROOT := $(abspath ../..)
PRODUCTS = strappy-un-probe
strappy-un-probe_TYPE = executable
strappy-un-probe_SOURCES = ../daemon/tests/user_notification_device/main.m
strappy-un-probe_CFLAGS = -fblocks
strappy-un-probe_LDFLAGS = -framework Foundation
strappy-un-probe_ENTITLEMENTS = ../daemon/tests/user_notification_device/Entitlements.plist
strappy-un-probe_SIGN = 1
strappy-un-probe_PACKAGES = zip
# Optional, removable sound fixture in the recipient app bundle. The separate
# package owns only its diagnostic executable and uniquely named sound file.
ifneq ($(PROBE_SOUND_FILE),)
strappy-un-probe_PACKAGES = deb-rootless
strappy-un-probe_PACKAGE_ID = com.altivecintelligence.strappy.sound-probe
strappy-un-probe_VERSION = 1.0
strappy-un-probe_PACKAGE_ARCH = all
strappy-un-probe_PACKAGE_FILES = $(PROBE_SOUND_FILE)=Applications/Strappy.app/StrappyManualChime.wav
endif
include $(ALTIVEC_ROOT)/altivec_common_phone.mk
endif
