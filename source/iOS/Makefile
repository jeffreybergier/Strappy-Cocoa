# Altivec Intelligence - Strappy (iPhone)
APP_NAME = Strappy
SOURCES = main.m AppDelegate.m XPUIKit.m
INFO_PLIST = Info.plist

SHARED_DIR = ../shared
OPENAI_C_DIR = ../deps/openai-c
OPENAI_C_COMPAT_DIR = ../deps/openai-c-altivec
ALTIVECCORE_REQUIRED = 1
ALTIVECCOCOA_REQUIRED = 1
SOURCE_DIRS = $(SHARED_DIR) $(OPENAI_C_DIR)/src
vpath %.m $(SHARED_DIR)
EXTRA_FLAGS = -I$(SHARED_DIR) -I$(OPENAI_C_DIR)/include -I$(OPENAI_C_COMPAT_DIR)/include
EXTRA_SOURCES = StrappyShared.m core.c chat.c image.c audio.c embeddings.c
PHONE_SOURCE_FLAGS = -fobjc-arc
PHONE_EXTRA_SOURCE_FLAGS = -Wno-conversion -Wno-sign-conversion -Wno-float-conversion -Wno-strict-prototypes -Wno-newline-eof
BUNDLE_LOCALIZATION_DIRS = $(SHARED_DIR)/Resources

VALIDATE_PATHS = . $(SHARED_DIR) $(OPENAI_C_DIR) $(OPENAI_C_COMPAT_DIR)

# Import common Phone build engine.
include /altivec/altivec_common_phone.mk
