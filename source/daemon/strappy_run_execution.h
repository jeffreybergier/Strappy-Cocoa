#ifndef STRAPPY_RUN_EXECUTION_H
#define STRAPPY_RUN_EXECUTION_H
#include "strappy_run.h"
unsigned int strappy_run_accept(const char *path, long long session,
    const char *key, const char *prompt, strappy_run_record *run);

unsigned int strappy_run_accept_if_idle(const char *path, long long session,
    const char *key, const char *prompt, strappy_run_record *run, int accept_new);

unsigned int strappy_run_accept_with_options(const char *path, long long session,
    const char *key, const char *prompt, strappy_run_record *run, int accept_new,
    const strappy_session_options *options, strappy_session_option_mask mask);

int strappy_run_update(const char *path, long long id, int state, int cancel,
    const char *kind, const char *reason, long long started, long long retry);

int strappy_run_progress(const char *path, long long id, const strappy_responses_event *event);

int strappy_run_reconcile(const char *path);

#endif
