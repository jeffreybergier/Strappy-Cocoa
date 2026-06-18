# Altivec Intelligence - Strappy (iPhone)
APP_NAME = Strappy
SOURCES = main.m AppDelegate.m XPUIKit.m
INFO_PLIST = Info.plist

SHARED_DIR = ../shared
ALTIVECCORE_REQUIRED = 1
ALTIVECCOCOA_REQUIRED = 1
SOURCE_DIRS = $(SHARED_DIR)
vpath %.m $(SHARED_DIR)
EXTRA_FLAGS = -I$(SHARED_DIR)
EXTRA_SOURCES = XPFoundation.m StrappySession.m strappy_core.c strappy_config.c strappy_client.c strappy_db.c strappy_assistant.c strappy_webview.c
PHONE_SOURCE_FLAGS = -fobjc-arc
PHONE_EXTRA_SOURCE_FLAGS = -Wno-conversion -Wno-sign-conversion -Wno-float-conversion -Wno-strict-prototypes -Wno-newline-eof
BUNDLE_LOCALIZATION_DIRS = $(SHARED_DIR)/Resources

VALIDATE_PATHS = . $(SHARED_DIR)

# Import common Phone build engine.
include /altivec/altivec_common_phone.mk
