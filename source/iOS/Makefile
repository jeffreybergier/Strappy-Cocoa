# Altivec Intelligence - Strappy (iPhone)
APP_NAME = Strappy
SOURCES = main.m AppDelegate.m XPUIKit.m
INFO_PLIST = Info.plist

SHARED_DIR = ../shared
ALTIVECCOCOA_REQUIRED = 1
SOURCE_DIRS = $(SHARED_DIR)
vpath %.m $(SHARED_DIR)
EXTRA_FLAGS = -I$(SHARED_DIR)
EXTRA_SOURCES = StrappyShared.m
PHONE_SOURCE_FLAGS = -fobjc-arc
BUNDLE_LOCALIZATION_DIRS = $(SHARED_DIR)/Resources

VALIDATE_PATHS = . $(SHARED_DIR)

# Import common Phone build engine.
include /altivec/altivec_common_phone.mk
