# Altivec Intelligence - Strappy (iPhone)
APP_NAME = Strappy
SOURCES = main.m AppDelegate.m StrappyRootCoordinator.m \
          SessionListViewController.m PreferencesTableViewController.m \
          MessageListViewController.m PromptSendViewController.m \
          StrappyIdleTimerAssertion.m \
          StrappyActivityAccessoryView.m \
          StrappyPreferencesWhitelistTableViewController.m \
          StrappyPreferencesModelWhitelistTableViewController.m \
          StrappyPreferencesDatabaseWhitelistTableViewController.m \
          StrappyPreferencesSystemPromptsTableViewController.m \
          XPUIKit.m
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
ALTIVECCOCOA_REQUIRED = 1
SOURCE_DIRS = $(SHARED_DIR)
vpath %.m $(SHARED_DIR)
EXTRA_FLAGS = -I$(SHARED_DIR)
EXTRA_SOURCES = XPFoundation.m XPKeychain.m StrappyKeychain.m StrappySession.m FileScanner.m strappy_core.c strappy_cocoa.c strappy_config.c strappy_client.c strappy_db.c strappy_model_catalog.c strappy_file_scanner.c strappy_file_read.c strappy_file_mutation.c strappy_file_write.c strappy_file_edit.c strappy_bash.c strappy_session.c strappy_prompt.c strappy_quality_policy.c strappy_assistant_sets.c strappy_tools.c strappy_web.c strappy_responses.c strappy_webview.c
PHONE_SOURCE_FLAGS = -fobjc-arc
PHONE_EXTRA_SOURCE_FLAGS = -Wno-conversion -Wno-sign-conversion -Wno-float-conversion -Wno-strict-prototypes -Wno-newline-eof
LIBS_IPHONE = -framework Security -framework QuartzCore
PHONE_LDID_SIGN ?= 1
PHONE_LDID_ENTITLEMENTS ?= Entitlements.plist
BUNDLE_LOCALIZATION_DIRS = $(SHARED_DIR)/Resources
EXTRA_BUNDLE_STEPS = @set -e; $(MAKE) -f Makefile $(FONTAWESOME_GENERATED); echo "  > copying prompt guidance resources"; for resource in $(GUIDANCE_RESOURCES); do cp "$$resource" "$@/"; done

DEB_PACKAGE_ID ?= com.altivecintelligence.strappy
DEB_VERSION ?= 0.1.0
DEB_ARCH ?= iphoneos-arm
DEB_SECTION ?= Applications
DEB_PRIORITY ?= optional
DEB_MAINTAINER ?= Altivec Intelligence <support@altivecintelligence.com>
DEB_DESCRIPTION ?= OpenRouter assistant for jailbroken iOS devices.
DEB_COMPRESSION ?= gzip
DEB_APP_PROCESS ?= $(APP_NAME)
DEB_STAGE = $(INT_DIR)/deb
PHONE_DEB = $(BUILD_DIR)/$(APP_NAME).deb

$(FONTAWESOME_GENERATED): $(FONTAWESOME_GENERATOR) $(FONTAWESOME_FONT_FILES)
	python3 $(FONTAWESOME_GENERATOR) --font-dir "$(FONTAWESOME_FONT_DIR)" --webview-map "$(FONTAWESOME_WEBVIEW_MAP)"

VALIDATE_PATHS = . $(SHARED_DIR)

# Import common Phone build engine.
include /altivec/altivec_common_phone.mk

.DEFAULT_GOAL := deb

deb: validate
	@echo "--- Building Phone Debian Package ($(OPT_FLAGS)) ---"
	@$(MAKE) --no-print-directory build-release/$(APP_NAME).deb BUILD_DIR=build-release OPT_FLAGS=-O3

debug-deb: validate
	@echo "--- Building Phone Debug Debian Package (-O0) ---"
	@$(MAKE) --no-print-directory build-debug/$(APP_NAME).deb BUILD_DIR=build-debug OPT_FLAGS=-O0

$(PHONE_DEB): $(APP_BUNDLE)
	@echo " [5/5] Packaging Debian archive..."
	@rm -rf "$(DEB_STAGE)"
	@mkdir -p "$(DEB_STAGE)/DEBIAN" "$(DEB_STAGE)/Applications"
	@cp -R "$(APP_BUNDLE)" "$(DEB_STAGE)/Applications/"
	@installed_size=$$(du -sk "$(DEB_STAGE)/Applications/$(APP_NAME).app" | awk '{print $$1}'); \
	{ \
	  echo "Package: $(DEB_PACKAGE_ID)"; \
	  echo "Name: $(APP_NAME)"; \
	  echo "Version: $(DEB_VERSION)"; \
	  echo "Architecture: $(DEB_ARCH)"; \
	  echo "Section: $(DEB_SECTION)"; \
	  echo "Priority: $(DEB_PRIORITY)"; \
	  echo "Maintainer: $(DEB_MAINTAINER)"; \
	  echo "Installed-Size: $$installed_size"; \
	  echo "Description: $(DEB_DESCRIPTION)"; \
	} > "$(DEB_STAGE)/DEBIAN/control"
	@{ \
	  echo '#!/bin/sh'; \
	  echo 'set -e'; \
	  echo ''; \
	  echo 'case "$$1" in'; \
	  echo '  install|upgrade)'; \
	  echo '    if command -v killall >/dev/null 2>&1; then'; \
	  echo '      killall $(DEB_APP_PROCESS) >/dev/null 2>&1 || true'; \
	  echo '      sleep 1'; \
	  echo '      killall -9 $(DEB_APP_PROCESS) >/dev/null 2>&1 || true'; \
	  echo '    fi'; \
	  echo '    ;;'; \
	  echo 'esac'; \
	  echo ''; \
	  echo 'exit 0'; \
	} > "$(DEB_STAGE)/DEBIAN/preinst"
	@{ \
	  echo '#!/bin/sh'; \
	  echo 'set -e'; \
	  echo ''; \
	  echo 'case "$$1" in'; \
	  echo '  remove|upgrade|deconfigure)'; \
	  echo '    if command -v killall >/dev/null 2>&1; then'; \
	  echo '      killall $(DEB_APP_PROCESS) >/dev/null 2>&1 || true'; \
	  echo '      sleep 1'; \
	  echo '      killall -9 $(DEB_APP_PROCESS) >/dev/null 2>&1 || true'; \
	  echo '    fi'; \
	  echo '    ;;'; \
	  echo 'esac'; \
	  echo ''; \
	  echo 'exit 0'; \
	} > "$(DEB_STAGE)/DEBIAN/prerm"
	@{ \
	  echo '#!/bin/sh'; \
	  echo 'set -e'; \
	  echo ''; \
	  echo 'case "$$1" in'; \
	  echo '  configure)'; \
	  echo '    if command -v uicache >/dev/null 2>&1; then'; \
	  echo '      su mobile -c uicache >/dev/null 2>&1 || uicache >/dev/null 2>&1 || true'; \
	  echo '    fi'; \
	  echo '    ;;'; \
	  echo 'esac'; \
	  echo ''; \
	  echo 'exit 0'; \
	} > "$(DEB_STAGE)/DEBIAN/postinst"
	@find "$(DEB_STAGE)" -type d -exec chmod 0755 {} \;
	@find "$(DEB_STAGE)" -type f -exec chmod 0644 {} \;
	@chmod 0755 "$(DEB_STAGE)/DEBIAN/preinst" "$(DEB_STAGE)/DEBIAN/prerm" "$(DEB_STAGE)/DEBIAN/postinst"
	@chmod 0755 "$(DEB_STAGE)/Applications/$(APP_NAME).app/$(APP_NAME)"
	@fakeroot sh -c 'chown -R 0:0 "$$1" && dpkg-deb -Z"$$3" --build "$$1" "$$2"' sh "$(DEB_STAGE)" "$@" "$(DEB_COMPRESSION)"

.PHONY: deb debug-deb
