# Altivec Intelligence - Strappy (Mac)
APP_NAME = Strappy
SOURCES = main.m AppDelegate.m XPAppKit.m
INFO_PLIST = Info.plist

SHARED_DIR = ../shared
OPENAI_C_DIR = ../deps/openai-c
OPENAI_C_COMPAT_DIR = ../deps/openai-c-altivec
ALTIVECCORE_REQUIRED = 1
ALTIVECCORE_LINKAGE = static
ALTIVECCOCOA_REQUIRED = 1
ALTIVECCOCOA_LINKAGE = static
SOURCE_DIRS = $(SHARED_DIR) $(OPENAI_C_DIR)/src
vpath %.m $(SHARED_DIR)
EXTRA_FLAGS = -I$(SHARED_DIR) -I$(OPENAI_C_DIR)/include -I$(OPENAI_C_COMPAT_DIR)/include
EXTRA_SOURCES = StrappyShared.m core.c chat.c image.c audio.c embeddings.c
BUNDLE_LOCALIZATION_DIRS = $(SHARED_DIR)/Resources

# Import common Mac build engine.
include /altivec/altivec_common_mac.mk

MAC_FLAGS += $(EXTRA_FLAGS)
CLANG_EXTRA_WARNINGS += -Wno-conversion -Wno-sign-conversion -Wno-float-conversion -Wno-strict-prototypes -Wno-newline-eof
VALIDATE_PATHS = . $(SHARED_DIR) $(OPENAI_C_DIR) $(OPENAI_C_COMPAT_DIR)
