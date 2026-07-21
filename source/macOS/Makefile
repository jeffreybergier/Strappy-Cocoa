# Altivec Intelligence - Strappy (Mac)
APP_NAME = Strappy
SOURCES = main.m AppDelegate.m XPAppKit.m PreferencesWindowController.m \
          StrappyPreferencesWhitelistView.m \
          StrappyPreferencesAuthenticationView.m \
          StrappyPreferencesModelWhitelistView.m \
          StrappyPreferencesDatabaseWhitelistView.m \
          StrappyPreferencesSystemPromptsView.m \
          SessionWindowController.m \
          SessionListViewController.m MessageListViewController.m \
          PromptSendViewController.m StrappyWindowAwareView.m \
          StrappyBottomToolbarView.m
INFO_PLIST = Info.plist

SHARED_DIR = ../shared
SYSTEM_PROMPT_RESOURCE = $(SHARED_DIR)/Resources/SystemPrompt.json
ASSISTANT_SET_RESOURCE = $(SHARED_DIR)/Resources/AssistantSets.json
FONTAWESOME_GENERATOR = $(SHARED_DIR)/tools/generate_fontawesome_webview_map.py
FONTAWESOME_FONT_DIR ?= /altivec/libs/cocoa/Resources/Fonts
FONTAWESOME_ICONS_RESOURCE = $(SHARED_DIR)/Resources/FontAwesomeIcons.json
FONTAWESOME_WEBVIEW_MAP = $(SHARED_DIR)/strappy_fontawesome_icons.inc
FONTAWESOME_GENERATED = $(FONTAWESOME_WEBVIEW_MAP)
FONTAWESOME_FONT_FILES = \
                     $(FONTAWESOME_FONT_DIR)/FA7-Solid-900.otf \
                     $(FONTAWESOME_FONT_DIR)/FA7-Regular-400.otf \
                     $(FONTAWESOME_FONT_DIR)/FA7-Brands-400.otf
GUIDANCE_RESOURCES = $(SYSTEM_PROMPT_RESOURCE) \
                     $(ASSISTANT_SET_RESOURCE) \
                     $(SHARED_DIR)/Resources/GuidanceTools.json \
                     $(SHARED_DIR)/Resources/GuidanceDatabase.json \
                     $(FONTAWESOME_ICONS_RESOURCE)
ALTIVECCORE_REQUIRED = 1
ALTIVECCORE_LINKAGE = dynamic
ALTIVECCOCOA_REQUIRED = 1
ALTIVECCOCOA_LINKAGE = static
SOURCE_DIRS = $(SHARED_DIR)
vpath %.m $(SHARED_DIR)
EXTRA_FLAGS = -I$(SHARED_DIR) -I$(ALTIVECCORE_DIR)/include
EXTRA_SOURCES = XPFoundation.m XPKeychain.m StrappyKeychain.m StrappySession.m FileScanner.m strappy_core.c strappy_cocoa.c strappy_config.c strappy_client.c strappy_db.c strappy_model_catalog.c strappy_file_scanner.c strappy_file_read.c strappy_file_mutation.c strappy_file_write.c strappy_file_edit.c strappy_bash.c strappy_session.c strappy_prompt.c strappy_quality_policy.c strappy_assistant_sets.c strappy_tools.c strappy_responses.c strappy_webview.c
BUNDLE_FONT_DIRS = $(ALTIVECCOCOA_DIR)/Resources/Fonts
BUNDLE_LOCALIZATION_DIRS = $(SHARED_DIR)/Resources
EXTRA_BUNDLE_STEPS = @set -e; $(MAKE) -f Makefile $(FONTAWESOME_GENERATED); echo "  > copying prompt guidance resources"; for resource in $(GUIDANCE_RESOURCES); do cp "$$resource" "$@/Contents/Resources/"; done

$(FONTAWESOME_GENERATED): $(FONTAWESOME_GENERATOR) $(FONTAWESOME_FONT_FILES)
	python3 $(FONTAWESOME_GENERATOR) --font-dir "$(FONTAWESOME_FONT_DIR)" --webview-map "$(FONTAWESOME_WEBVIEW_MAP)"

# Import common Mac build engine.
include /altivec/altivec_common_mac.mk

MAC_FLAGS += $(EXTRA_FLAGS)
MAC_LIBS += -framework Security
CLANG_EXTRA_WARNINGS += -Wno-conversion -Wno-sign-conversion -Wno-float-conversion -Wno-strict-prototypes -Wno-newline-eof
VALIDATE_PATHS = . $(SHARED_DIR)
