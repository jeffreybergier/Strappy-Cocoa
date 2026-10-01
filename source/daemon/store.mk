# Portable storage owner; link the same Altivec SQLite build as the app.
STRAPPY_STORE_SOURCES = ../shared/strappy_db.c ../shared/strappy_db_catalog.c \
 ../shared/strappy_db_sidebar.c ../shared/strappy_db_sessions.c \
 ../shared/strappy_db_responses.c ../shared/strappy_core.c \
 ../shared/strappy_provider.c ../shared/strappy_config.c \
 ../shared/strappy_assistant_sets.c ../shared/strappy_bundled_catalog.c

STRAPPY_EXECUTION_SOURCES = ../daemon/strappy_execution.c ../shared/strappy_run.c \
 ../shared/strappy_client.c ../shared/strappy_sse.c ../shared/strappy_identity.c \
 ../shared/strappy_cocoa.c ../shared/strappy_calendar.c ../shared/strappy_model_catalog.c \
 ../shared/strappy_prompt.c ../shared/strappy_quality_policy.c ../shared/strappy_debug_capture.c \
 ../shared/strappy_responses.c ../shared/strappy_session.c ../shared/strappy_tools.c \
 ../shared/strappy_skills.c ../shared/strappy_study.c ../shared/strappy_bash.c \
 ../shared/strappy_file_read.c ../shared/strappy_file_mutation.c ../shared/strappy_file_write.c \
 ../shared/strappy_file_edit.c ../shared/strappy_webview.c ../shared/strappy_openai_oauth.c
STRAPPY_CREDENTIAL_SOURCES = ../daemon/StrappySession.m ../shared/StrappyKeychain.m \
 ../shared/XPKeychain.m ../shared/XPFoundation.m ../shared/XPFoundationCalendar.m
