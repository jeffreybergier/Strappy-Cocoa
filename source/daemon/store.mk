STRAPPY_SOURCE_ROOT ?= ..
# Portable storage owner; link the same Altivec SQLite build as the app.
STRAPPY_STORE_SOURCES = $(STRAPPY_SOURCE_ROOT)/shared/strappy_db.c $(STRAPPY_SOURCE_ROOT)/shared/strappy_db_catalog.c \
 $(STRAPPY_SOURCE_ROOT)/shared/strappy_db_sidebar.c $(STRAPPY_SOURCE_ROOT)/shared/strappy_db_sessions.c \
 $(STRAPPY_SOURCE_ROOT)/shared/strappy_db_responses.c $(STRAPPY_SOURCE_ROOT)/shared/strappy_core.c \
 $(STRAPPY_SOURCE_ROOT)/shared/strappy_options.c $(STRAPPY_SOURCE_ROOT)/shared/strappy_provider.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_config.c \
 $(STRAPPY_SOURCE_ROOT)/shared/strappy_assistant_sets.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_bundled_catalog.c

STRAPPY_EXECUTION_SOURCES = $(STRAPPY_SOURCE_ROOT)/daemon/strappy_auth.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_mutations.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_file_scanner.c \
 $(STRAPPY_SOURCE_ROOT)/daemon/strappy_attention.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_notifications.c $(STRAPPY_SOURCE_ROOT)/shared/XPFoundation/strappy_notification_platform.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_execution.c $(STRAPPY_SOURCE_ROOT)/shared/strappy_run.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_run_execution.c \
 $(STRAPPY_SOURCE_ROOT)/daemon/strappy_client.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_sse.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_identity.c \
 $(STRAPPY_SOURCE_ROOT)/shared/strappy_cocoa.c $(STRAPPY_SOURCE_ROOT)/shared/strappy_calendar.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_model_catalog.c \
 $(STRAPPY_SOURCE_ROOT)/shared/strappy_prompt.c $(STRAPPY_SOURCE_ROOT)/shared/strappy_quality_policy.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_debug_capture.c \
 $(STRAPPY_SOURCE_ROOT)/daemon/strappy_responses.c $(STRAPPY_SOURCE_ROOT)/shared/strappy_session.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_session_execution.c $(STRAPPY_SOURCE_ROOT)/shared/strappy_tool_metadata.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_tools.c \
 $(STRAPPY_SOURCE_ROOT)/daemon/strappy_skills.c $(STRAPPY_SOURCE_ROOT)/shared/strappy_study.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_study_execution.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_bash.c \
 $(STRAPPY_SOURCE_ROOT)/daemon/strappy_file_read.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_file_mutation.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_file_write.c \
 $(STRAPPY_SOURCE_ROOT)/daemon/strappy_file_edit.c $(STRAPPY_SOURCE_ROOT)/shared/strappy_webview.c $(STRAPPY_SOURCE_ROOT)/daemon/strappy_openai_oauth.c
STRAPPY_CREDENTIAL_SOURCES = $(STRAPPY_SOURCE_ROOT)/daemon/StrappySession.m $(STRAPPY_SOURCE_ROOT)/daemon/StrappyKeychain.m \
 $(STRAPPY_SOURCE_ROOT)/daemon/XPKeychain.m $(STRAPPY_SOURCE_ROOT)/shared/XPFoundation.m $(STRAPPY_SOURCE_ROOT)/shared/XPFoundationCalendar.m
