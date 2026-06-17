# Altivec Intelligence - Strappy (Mac)
APP_NAME = Strappy
SOURCES = main.m AppDelegate.m XPAppKit.m
INFO_PLIST = Info.plist

SHARED_DIR = ../shared
ALTIVECCORE_REQUIRED = 1
ALTIVECCORE_LINKAGE = static
ALTIVECCOCOA_REQUIRED = 1
ALTIVECCOCOA_LINKAGE = static
SOURCE_DIRS = $(SHARED_DIR)
vpath %.m $(SHARED_DIR)
EXTRA_FLAGS = -I$(SHARED_DIR)
EXTRA_SOURCES = StrappySession.m strappy_core.c strappy_config.c strappy_client.c strappy_db.c strappy_assistant.c
BUNDLE_LOCALIZATION_DIRS = $(SHARED_DIR)/Resources

# Import common Mac build engine.
include /altivec/altivec_common_mac.mk

MAC_FLAGS += $(EXTRA_FLAGS)
CLANG_EXTRA_WARNINGS += -Wno-conversion -Wno-sign-conversion -Wno-float-conversion -Wno-strict-prototypes -Wno-newline-eof
VALIDATE_PATHS = . $(SHARED_DIR)
