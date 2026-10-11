ALTIVEC_ROOT ?= /altivec
include $(ALTIVEC_ROOT)/make/entry.mk
ifeq ($(_altivec_relocated),)
PROJECT_ROOT := $(abspath $(dir $(firstword $(MAKEFILE_LIST)))/../..)
include ../daemon/store.mk
STRAPPY_IPC_SOURCES = ../shared/strappy_ipc.c ../shared/XPFoundation/strappy_ipc_platform.c ../daemon/strappy_ipc_server.c
GROUP_ui_SOURCES = main.m AppDelegate.m StrappyRootCoordinator.m \
          StrappyAuthentication.m \
          StrappyAccountTableViewController.m \
          StrappyAppearance.m \
          SessionListViewController.m PreferencesTableViewController.m \
          MessageListViewController.m PromptSendViewController.m \
          StrappySessionOptionsTableViewController.m \
          StrappyModelCellFormatter.m \
          StrappyIdleTimerAssertion.m \
          StrappyActivityAccessoryView.m \
          StrappyPreferencesStatusToolbarView.m \
          StrappyPreferencesWhitelistTableViewController.m \
          StrappyPreferencesModelWhitelistTableViewController.m \
          StrappyModelProvidersTableViewController.m \
          StrappyPreferencesDatabaseWhitelistTableViewController.m \
          StrappyPreferencesDatabaseStudyViewController.m \
          StrappyPreferencesSystemPromptsTableViewController.m \
          XPUIKit.m XPWebView.m
INFO_PLIST = Info.plist

SHARED_DIR = ../shared
SYSTEM_PROMPT_RESOURCE = $(SHARED_DIR)/Resources/SystemPrompt.json
ASSISTANT_SET_RESOURCE = $(SHARED_DIR)/Resources/AssistantSets.json
BUNDLED_MODELS_RESOURCE = $(SHARED_DIR)/Resources/BundledModels.json
FONTAWESOME_GENERATOR = $(SHARED_DIR)/tools/generate_fontawesome_webview_map.py
FONTAWESOME_FONT_DIR ?= $(ALTIVECCOCOA_DIR)/Resources/Fonts
FONTAWESOME_ICONS_RESOURCE = $(SHARED_DIR)/Resources/FontAwesomeIcons.json
FONTAWESOME_WEBVIEW_MAP = $(GENERATED_DIR)/strappy_fontawesome_icons.inc
FONTAWESOME_GENERATED = $(FONTAWESOME_WEBVIEW_MAP)
FONTAWESOME_FONT_FILES = \
                     $(FONTAWESOME_FONT_DIR)/FA7-Solid-900.otf \
                     $(FONTAWESOME_FONT_DIR)/FA7-Regular-400.otf \
                     $(FONTAWESOME_FONT_DIR)/FA7-Brands-400.otf
SHARED_RESOURCES = $(SYSTEM_PROMPT_RESOURCE) \
                   $(ASSISTANT_SET_RESOURCE) \
                   $(BUNDLED_MODELS_RESOURCE) \
                   $(SHARED_DIR)/Resources/GuidanceTools.json \
                   $(SHARED_DIR)/Resources/GuidanceSkills.json \
                   $(SHARED_DIR)/Resources/GuidanceDatabase.json \
                   $(FONTAWESOME_ICONS_RESOURCE)
SOURCE_DIRS = $(SHARED_DIR) $(SHARED_DIR)/XPFoundation
EXTRA_FLAGS = -DSTRAPPY_DB_UI=1 -I$(SHARED_DIR)
GROUP_shared_SOURCES = strappy_run.c strappy_store_client.c strappy_ipc.c strappy_ipc_platform.c StrappyAsync.m StrappyModelRows.m StrappySessionSections.m XPFoundationCalendar.m XPFoundation.m StrappySession.m FileScanner.m strappy_core.c strappy_cocoa.c strappy_calendar.c strappy_provider.c strappy_db.c strappy_db_catalog.c strappy_db_sidebar.c strappy_db_sessions.c strappy_db_responses.c strappy_session.c strappy_prompt.c strappy_quality_policy.c strappy_assistant_sets.c strappy_study.c strappy_webview.c strappy_options.c strappy_tool_metadata.c
LIBS_IPHONE = -framework QuartzCore -weak_framework WebKit
BUNDLE_LOCALIZATION_DIRS = $(SHARED_DIR)/Resources

PRODUCTS = Strappy strappyd
CPPFLAGS += -DSTRAPPY_FONTAWESOME_MAP=\"$(FONTAWESOME_WEBVIEW_MAP)\"
Strappy_TYPE = app
Strappy_GROUPS = ui shared
Strappy_PLIST = $(INFO_PLIST)
Strappy_FLAGS = $(EXTRA_FLAGS)
Strappy_RESOURCES = $(filter-out Resources/Info.plist,$(wildcard Resources/*)) $(SHARED_RESOURCES)
Strappy_LOCALIZATIONS = $(BUNDLE_LOCALIZATION_DIRS)
Strappy_GENERATED = $(FONTAWESOME_GENERATED)
Strappy_COCOA = static
strappyd_TYPE = daemon
strappyd_SOURCES = ../daemon/strappyd.c $(STRAPPY_IPC_SOURCES) $(STRAPPY_STORE_SOURCES) $(STRAPPY_EXECUTION_SOURCES) $(STRAPPY_CREDENTIAL_SOURCES)
strappyd_CORE = static
strappyd_FLAGS = -DSTRAPPY_DB_WRITER=1 -I../shared -I../daemon
strappyd_GENERATED = $(FONTAWESOME_GENERATED)
strappyd_LDFLAGS = -framework Foundation -framework Security -framework CoreFoundation -framework SystemConfiguration
GENERATORS = fontawesome
GEN_fontawesome_OUTPUT = $(FONTAWESOME_GENERATED)
GEN_fontawesome_INPUTS = $(FONTAWESOME_GENERATOR) $(FONTAWESOME_FONT_FILES)
GEN_fontawesome_COMMAND = python3 $(FONTAWESOME_GENERATOR) --font-dir "$(FONTAWESOME_FONT_DIR)" --webview-map "$(FONTAWESOME_WEBVIEW_MAP)"
Strappy_CORE = static
GROUP_ui_MEMORY = arc
Strappy_LDFLAGS = -framework UIKit -framework Foundation -framework CoreGraphics $(LIBS_IPHONE)
Strappy_ENTITLEMENTS = Entitlements.plist
Strappy_SANDBOX = unsandboxed
strappyd_ENTITLEMENTS = daemon/Entitlements.plist
Strappy_INSTALL = strappyd
Strappy_PACKAGES = deb-rootful deb-rootless
Strappy_PACKAGE_ARCH = all
Strappy_PACKAGE_ID = com.altivecintelligence.strappy
Strappy_VERSION := $(shell altivec-release current)
Strappy_LAUNCHD = daemon/com.altivecintelligence.strappyd.plist.in
Strappy_PACKAGE_SCRIPTS = daemon/preinst.in daemon/prerm.in daemon/postinst.in
Strappy_LIFECYCLE = daemon/lifecycle.sh.in
include $(ALTIVEC_ROOT)/altivec_common_phone.mk
endif
