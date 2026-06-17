# Altivec Intelligence - Strappy (Mac)
APP_NAME = Strappy
SOURCES = main.m AppDelegate.m XPAppKit.m
INFO_PLIST = Info.plist

SHARED_DIR = ../shared
ALTIVECCOCOA_REQUIRED = 1
ALTIVECCOCOA_LINKAGE = static
SOURCE_DIRS = $(SHARED_DIR)
vpath %.m $(SHARED_DIR)
EXTRA_FLAGS = -I$(SHARED_DIR)
EXTRA_SOURCES = StrappyShared.m
BUNDLE_LOCALIZATION_DIRS = $(SHARED_DIR)/Resources

# Import common Mac build engine.
include /altivec/altivec_common_mac.mk

MAC_FLAGS += $(EXTRA_FLAGS)
VALIDATE_PATHS = . $(SHARED_DIR)
