# Altivec Intelligence - Strappy (Mac)
APP_NAME = Strappy
SOURCES = main.m AppDelegate.m XPAppKit.m PreferencesWindowController.m \
          SessionWindowController.m \
          SessionListViewController.m MessageListViewController.m \
          PromptSendViewController.m
INFO_PLIST = Info.plist

SHARED_DIR = ../shared
SYSTEM_PROMPT_TEMPLATE = $(SHARED_DIR)/Resources/PromptSystem.txt
ALTIVECCORE_REQUIRED = 1
ALTIVECCORE_LINKAGE = dynamic
ALTIVECCOCOA_REQUIRED = 1
ALTIVECCOCOA_LINKAGE = static
SOURCE_DIRS = $(SHARED_DIR)
vpath %.m $(SHARED_DIR)
EXTRA_FLAGS = -I$(SHARED_DIR) -I$(ALTIVECCORE_DIR)/include
EXTRA_SOURCES = XPFoundation.m StrappySession.m FileScanner.m strappy_core.c strappy_config.c strappy_client.c strappy_db.c strappy_file_scanner.c strappy_prompt.c strappy_tools.c strappy_assistant.c strappy_webview.c
BUNDLE_LOCALIZATION_DIRS = $(SHARED_DIR)/Resources
EXTRA_BUNDLE_STEPS = @echo "  > copying system prompt template"; cp "$(SYSTEM_PROMPT_TEMPLATE)" "$@/Contents/Resources/"

# Import common Mac build engine.
include /altivec/altivec_common_mac.mk

MAC_FLAGS += $(EXTRA_FLAGS)
CLANG_EXTRA_WARNINGS += -Wno-conversion -Wno-sign-conversion -Wno-float-conversion -Wno-strict-prototypes -Wno-newline-eof
VALIDATE_PATHS = . $(SHARED_DIR)
