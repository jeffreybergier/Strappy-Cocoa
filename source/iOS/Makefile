# Altivec Intelligence - Strappy (iPhone)
APP_NAME = Strappy
SOURCES = main.m AppDelegate.m StrappyRootCoordinator.m \
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
FONTAWESOME_WEBVIEW_MAP = $(SHARED_DIR)/strappy_fontawesome_icons.inc
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
ALTIVECCORE_REQUIRED = 1
ALTIVECCOCOA_REQUIRED = 1
SOURCE_DIRS = $(SHARED_DIR)
vpath %.m $(SHARED_DIR)
EXTRA_FLAGS = -I$(SHARED_DIR)
EXTRA_SOURCES = XPFoundation.m XPKeychain.m StrappyKeychain.m StrappySession.m FileScanner.m strappy_core.c strappy_cocoa.c strappy_provider.c strappy_config.c strappy_client.c strappy_sse.c strappy_identity.c strappy_openai_oauth.c strappy_db.c strappy_db_catalog.c strappy_db_sessions.c strappy_db_responses.c strappy_model_catalog.c strappy_file_scanner.c strappy_file_read.c strappy_file_mutation.c strappy_file_write.c strappy_file_edit.c strappy_bash.c strappy_session.c strappy_prompt.c strappy_quality_policy.c strappy_assistant_sets.c strappy_skills.c strappy_study.c strappy_tools.c strappy_responses.c strappy_webview.c
PHONE_SOURCE_FLAGS = -fobjc-arc
PHONE_EXTRA_SOURCE_FLAGS = -Wno-conversion -Wno-sign-conversion -Wno-float-conversion -Wno-strict-prototypes -Wno-newline-eof
LIBS_IPHONE = -framework Security -framework QuartzCore -weak_framework WebKit
PHONE_LDID_SIGN ?= 1
PHONE_LDID_ENTITLEMENTS ?= Entitlements.plist
BUNDLE_LOCALIZATION_DIRS = $(SHARED_DIR)/Resources
EXTRA_BUNDLE_STEPS = @set -e; $(MAKE) -f Makefile $(FONTAWESOME_GENERATED); echo "  > copying shared resources"; for resource in $(SHARED_RESOURCES); do cp "$$resource" "$@/"; done

DEB_PACKAGE_ID ?= com.altivecintelligence.strappy
# Keep package metadata aligned with the plist versions managed by the release
# helper. Rootful and rootless packages use different installation prefixes,
# while sharing the same universal armv7/arm64 app and architecture-independent
# package metadata.
DEB_VERSION ?= $(shell altivec-release current)
DEB_UNIVERSAL_ARCH ?= all
DEB_SECTION ?= Applications
DEB_PRIORITY ?= optional
DEB_MAINTAINER ?= Altivec Intelligence <support@altivecintelligence.com>
DEB_DESCRIPTION ?= OpenRouter assistant for jailbroken iOS devices.
DEB_COMPRESSION ?= gzip
DEB_APP_PROCESS ?= $(APP_NAME)
DEB_STAGE = $(INT_DIR)/deb-$(DEB_VARIANT)
DEB_APP_DIR = $(DEB_STAGE)$(DEB_INSTALL_PREFIX)/Applications
PHONE_DEB_ROOTFUL = $(BUILD_DIR)/$(APP_NAME)-rootful.deb
PHONE_DEB_ROOTLESS = $(BUILD_DIR)/$(APP_NAME)-rootless.deb
PHONE_DEBS = $(PHONE_DEB_ROOTFUL) $(PHONE_DEB_ROOTLESS)

VALIDATE_PATHS = . $(SHARED_DIR)

# The common Phone engine also exposes IPA packaging targets. Keep that engine
# private so Strappy's public build targets only produce Debian packages.
ifeq ($(STRAPPY_PHONE_INTERNAL_BUILD),1)
ALTIVEC_ROOT ?= /altivec
include $(ALTIVEC_ROOT)/altivec_common_phone.mk

$(FONTAWESOME_GENERATED): $(FONTAWESOME_GENERATOR) $(FONTAWESOME_FONT_FILES)
	python3 $(FONTAWESOME_GENERATOR) --font-dir "$(FONTAWESOME_FONT_DIR)" --webview-map "$(FONTAWESOME_WEBVIEW_MAP)"

.DEFAULT_GOAL := __deb

__deb: validate
	@set -e; configured_version="$$(altivec-release current)"; \
	if [ "$(DEB_VERSION)" != "$$configured_version" ]; then \
	  echo " [!] ERROR: Debian version $(DEB_VERSION) does not match configured app version $$configured_version"; \
	  exit 1; \
	fi
	@echo "--- Building Phone $(DEB_BUILD_LABEL) Debian Packages ($(OPT_FLAGS)) ---"
	@$(MAKE) --no-print-directory $(PHONE_DEBS) BUILD_DIR=$(BUILD_DIR) OPT_FLAGS=$(OPT_FLAGS)

$(PHONE_DEB_ROOTFUL): DEB_VARIANT = rootful
$(PHONE_DEB_ROOTFUL): DEB_ARCH = $(DEB_UNIVERSAL_ARCH)
$(PHONE_DEB_ROOTFUL): DEB_INSTALL_PREFIX =
$(PHONE_DEB_ROOTLESS): DEB_VARIANT = rootless
$(PHONE_DEB_ROOTLESS): DEB_ARCH = $(DEB_UNIVERSAL_ARCH)
$(PHONE_DEB_ROOTLESS): DEB_INSTALL_PREFIX = /var/jb

$(PHONE_DEBS): $(APP_BUNDLE)
	@echo " [5/5] Packaging $(DEB_VARIANT) Debian archive..."
	@rm -rf "$(DEB_STAGE)"
	@mkdir -p "$(DEB_STAGE)/DEBIAN" "$(DEB_APP_DIR)"
	@cp -R "$(APP_BUNDLE)" "$(DEB_APP_DIR)/"
	@if [ "$(PHONE_LDID_SIGN)" = "1" ] || [ -n "$(PHONE_LDID_ENTITLEMENTS)" ]; then \
		flags="-S"; \
		if [ -n "$(PHONE_LDID_ENTITLEMENTS)" ]; then flags="-S$(PHONE_LDID_ENTITLEMENTS)"; fi; \
		echo "  > ldid-signing staged $(APP_NAME).app bundle"; \
		$(PHONE_LDID) "$$flags" "$(DEB_APP_DIR)/$(APP_NAME).app"; \
	fi
	@installed_size=$$(du -sk "$(DEB_APP_DIR)/$(APP_NAME).app" | awk '{print $$1}'); \
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
	  echo '    if command -v uicache >/dev/null 2>&1 && [ -d "$(DEB_INSTALL_PREFIX)/Applications/$(APP_NAME).app" ]; then'; \
	  echo '      su mobile -c "uicache -u $(DEB_INSTALL_PREFIX)/Applications/$(APP_NAME).app" >/dev/null 2>&1 || uicache -u "$(DEB_INSTALL_PREFIX)/Applications/$(APP_NAME).app" >/dev/null 2>&1 || true'; \
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
	  echo '    if [ "$$1" != upgrade ] && command -v uicache >/dev/null 2>&1 && [ -d "$(DEB_INSTALL_PREFIX)/Applications/$(APP_NAME).app" ]; then'; \
	  echo '      su mobile -c "uicache -u $(DEB_INSTALL_PREFIX)/Applications/$(APP_NAME).app" >/dev/null 2>&1 || uicache -u "$(DEB_INSTALL_PREFIX)/Applications/$(APP_NAME).app" >/dev/null 2>&1 || true'; \
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
	  echo '      su mobile -c "uicache -p $(DEB_INSTALL_PREFIX)/Applications/$(APP_NAME).app" >/dev/null 2>&1 || uicache -p "$(DEB_INSTALL_PREFIX)/Applications/$(APP_NAME).app" >/dev/null 2>&1 || su mobile -c uicache >/dev/null 2>&1 || uicache >/dev/null 2>&1 || true'; \
	  echo '    fi'; \
	  echo '    ;;'; \
	  echo 'esac'; \
	  echo ''; \
	  echo 'exit 0'; \
	} > "$(DEB_STAGE)/DEBIAN/postinst"
	@find "$(DEB_STAGE)" -type d -exec chmod 0755 {} \;
	@find "$(DEB_STAGE)" -type f -exec chmod 0644 {} \;
	@chmod 0755 "$(DEB_STAGE)/DEBIAN/preinst" "$(DEB_STAGE)/DEBIAN/prerm" "$(DEB_STAGE)/DEBIAN/postinst"
	@chmod 0755 "$(DEB_APP_DIR)/$(APP_NAME).app/$(APP_NAME)"
	@fakeroot sh -c 'chown -R 0:0 "$$1" && dpkg-deb -Z"$$3" --build "$$1" "$$2"' sh "$(DEB_STAGE)" "$@" "$(DEB_COMPRESSION)"

.PHONY: __deb

else

.DEFAULT_GOAL := release

release:
	@$(MAKE) --no-print-directory STRAPPY_PHONE_INTERNAL_BUILD=1 __deb \
		BUILD_DIR=build-release OPT_FLAGS=-O3 DEB_BUILD_LABEL=Release

debug:
	@$(MAKE) --no-print-directory STRAPPY_PHONE_INTERNAL_BUILD=1 __deb \
		BUILD_DIR=build-debug OPT_FLAGS=-O0 DEB_BUILD_LABEL=Debug

clean analyze:
	@$(MAKE) --no-print-directory STRAPPY_PHONE_INTERNAL_BUILD=1 $@

.PHONY: clean debug release analyze

endif
