# Isolated experiment reusing the real daemon inventory and common build rules.
ALTIVEC_ROOT ?= /altivec
include $(ALTIVEC_ROOT)/make/entry.mk
ifeq ($(_altivec_relocated),)
override PRODUCTS = strappyd-bulletin-probe StrappyBulletinProbe
PACKAGE_PRODUCTS = strappyd-bulletin-probe
PROBE_SOURCE = ../daemon/tests/bulletin_probe
PROBE_INSTALL = Library/MobileSubstrate/DynamicLibraries/StrappyBulletinProbe
strappyd-bulletin-probe_TYPE = daemon
strappyd-bulletin-probe_SOURCES = $(strappyd_SOURCES) $(PROBE_SOURCE)/probe.c
strappyd-bulletin-probe_FLAGS = $(strappyd_FLAGS) -DSTRAPPY_BULLETIN_PROBE=1
strappyd-bulletin-probe_GROUPS = $(strappyd_GROUPS)
strappyd-bulletin-probe_CORE = static
strappyd-bulletin-probe_COCOA = static
strappyd-bulletin-probe_GENERATED = $(strappyd_GENERATED)
strappyd-bulletin-probe_LDFLAGS = $(strappyd_LDFLAGS)
strappyd-bulletin-probe_ENTITLEMENTS = $(PROBE_SOURCE)/Entitlements.plist
strappyd-bulletin-probe_PACKAGES = deb-rootful
strappyd-bulletin-probe_PACKAGE_ID = com.altivecintelligence.strappy.bulletin-probe
strappyd-bulletin-probe_PACKAGE_ARCH = iphoneos-arm
strappyd-bulletin-probe_VERSION = 0.1.0
strappyd-bulletin-probe_INSTALL = StrappyBulletinProbe
strappyd-bulletin-probe_PACKAGE_FILES = \
  $(PROBE_SOURCE)/StrappyBulletinProbe.plist=$(PROBE_INSTALL).plist
# The common executable pipeline supports a dylib through linker flags; INSTALL
# packages its signed output at the explicit MobileSubstrate path.
StrappyBulletinProbe_TYPE = executable
StrappyBulletinProbe_SOURCES = $(PROBE_SOURCE)/XPBulletinProbe.m
StrappyBulletinProbe_LDFLAGS = -dynamiclib -install_name /$(PROBE_INSTALL).dylib \
  -framework Foundation -framework CoreFoundation -lobjc
StrappyBulletinProbe_SIGN = 1
StrappyBulletinProbe_INSTALL_PATH = $(PROBE_INSTALL).dylib
include Makefile
endif
