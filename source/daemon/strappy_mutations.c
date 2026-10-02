#include "strappy_mutations.h"
#include "../shared/strappy_session.h"
#include "../shared/strappy_study.h"
#include "../shared/strappy_core.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>

const char *strappy_command_text(const cJSON *object, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}
long long strappy_command_number(const cJSON *object, const char *key)
{
    const char *s = strappy_command_text(object, key);
    char *end; long long n;
    const char *digit;
    for (digit = s; *digit; ++digit) if (*digit < '0' || *digit > '9') return -1;
    errno = 0; n = strtoll(s, &end, 10);
    return !*s || *end || errno ? -1 : n;
}
int strappy_command_options(const cJSON *c, strappy_session_options *input,
    strappy_session_option_mask *mask)
{
    long long value = strappy_command_number(c,"mask");
    const char *booleans[] = { "web", "bash", "one_tool", "quality" };
    unsigned int i;
    strappy_session_options_init(input);
    if (value < 0 || (value & ~STRAPPY_SESSION_OPTION_ALL) ||
        strappy_command_number(c,"rounds") < 1 ||
        strappy_command_number(c,"rounds") > STRAPPY_SESSION_MAX_LIMIT) return 0;
    for (i = 0; i < 4; ++i)
        if (strappy_command_number(c,booleans[i]) < 0 || strappy_command_number(c,booleans[i]) > 1) return 0;
    *mask = (strappy_session_option_mask)value;
    input->model_id = (char *)strappy_command_text(c,"model");
    input->provider_account_id = (char *)strappy_command_text(c,"account");
    input->assistant_set_id = (char *)strappy_command_text(c,"assistant");
    input->working_directory = (char *)strappy_command_text(c,"directory");
    input->round_limit = (long)strappy_command_number(c,"rounds");
    input->web_search_enabled = (int)strappy_command_number(c,"web");
    input->bash_enabled = (int)strappy_command_number(c,"bash");
    input->limit_to_one_tool = (int)strappy_command_number(c,"one_tool");
    input->answer_quality_enabled = (int)strappy_command_number(c,"quality");
    return strappy_web_provider_parse(strappy_command_text(c,"web_provider"),&input->web_provider);
}
int strappy_mutation(const char *db, const char *resources, const cJSON *c, char **result)
{
    const char *action = strappy_command_text(c, "action");
    const char *id = strappy_command_text(c, "id");
    long long session = strappy_command_number(c, "session");
    int ok = 0;
    char *error = NULL;
    *result = NULL;
    if (!strcmp(action, "create_session")) {
        char number[32];
        ok = strappy_session_create_with_working_directory(db, strappy_command_text(c,"directory"), &session, &error);
        if (ok) { snprintf(number, sizeof(number), "%lld", session); *result = strappy_string_duplicate(number); }
    } else if (!strcmp(action, "delete_session")) {
        ok = session > 0 && strappy_session_delete(db, session, &error);
    } else if (!strcmp(action, "default_model")) {
        ok = strappy_session_set_default_model(db, id, &error);
    } else if (!strcmp(action, "approve_model")) {
        long long value = strappy_command_number(c,"value");
        ok = (value == 0 || value == 1) && strappy_session_set_model_allowed(db,id,(int)value,&error);
    } else if (!strcmp(action, "approve_database") || !strcmp(action, "hide_database")) {
        long long catalog = strappy_command_number(c,"id"), value = strappy_command_number(c,"value");
        if (catalog > 0 && (value == 0 || value == 1))
            ok = !strcmp(action,"approve_database") ?
                strappy_db_update_discovered_database_decision(db,catalog,value ? "allowed" : "unknown",&error) :
                strappy_db_update_discovered_database_hidden(db,catalog,(int)value,&error);
    } else if (!strcmp(action,"delete_study")) {
        ok = strappy_study_delete_database_values(db,id,&error);
    } else if (!strcmp(action,"reset_study")) {
        ok = strappy_study_reset(db,&error);
    } else if (!strcmp(action,"include_context")) {
        long long request = strappy_command_number(c,"id"), value = strappy_command_number(c,"value");
        ok = session > 0 && request > 0 && (value == 0 || value == 1) &&
            strappy_session_update_model_request_include_in_context(db,session,request,(int)value,&error);
    } else if (!strcmp(action,"options") || !strcmp(action,"defaults")) {
        strappy_session_options input, saved;
        strappy_session_option_mask changed;
        strappy_session_option_mask mask = 0;
        strappy_session_options_init(&saved);
        if (strappy_command_options(c,&input,&mask)) {
            if (!strcmp(action,"defaults"))
                ok = strappy_session_update_default_options(db,strappy_command_text(c,"fallback"),resources,
                    &input,(strappy_session_option_mask)mask,&saved,&changed,&error);
            else if (session > 0)
                ok = strappy_session_update_options(db,session,resources,&input,
                    (strappy_session_option_mask)mask,&saved,&changed,&error);
        }
        strappy_session_options_destroy(&saved);
    } else if (!strcmp(action,"create_model") || !strcmp(action,"update_model")) {
        strappy_manual_model_input input;
        memset(&input,0,sizeof(input));
        input.wire_model_id = strappy_command_text(c,"wire");
        input.display_name = strappy_command_text(c,"name");
        input.context_window_tokens = strappy_command_number(c,"context");
        input.max_output_tokens = strappy_command_number(c,"output");
        input.reasoning_enabled = (int)strappy_command_number(c,"reasoning");
        input.image_input_enabled = (int)strappy_command_number(c,"image");
        input.local_functions_enabled = (int)strappy_command_number(c,"functions");
        input.pricing_prompt = strappy_command_text(c,"price_input");
        input.pricing_completion = strappy_command_text(c,"price_output");
        input.pricing_input_cache_read = strappy_command_text(c,"price_read");
        input.pricing_input_cache_write = strappy_command_text(c,"price_write");
        if (strappy_command_number(c,"reasoning") < 0 || strappy_command_number(c,"reasoning") > 1 ||
            strappy_command_number(c,"image") < 0 || strappy_command_number(c,"image") > 1 ||
            strappy_command_number(c,"functions") < 0 || strappy_command_number(c,"functions") > 1) return 0;
        ok = !strcmp(action,"create_model") ? strappy_db_create_manual_model(db,id,&input,result,&error) :
            strappy_db_update_manual_model(db,id,&input,&error);
    } else if (!strcmp(action,"archive_model")) {
        ok = strappy_db_archive_manual_model(db,id,strappy_command_text(c,"wire"),&error);
    } else if (!strcmp(action,"create_account")) {
        ok = strappy_db_create_provider_account(db,id,strappy_command_text(c,"name"),NULL,result,&error);
    } else if (!strcmp(action,"update_account") || !strcmp(action,"archive_account") ||
               !strcmp(action,"delete_oauth") || !strcmp(action,"save_oauth") || !strcmp(action,"refresh_oauth")) {
        ok = strappy_service_account_command(db,c,result);
    }
    /* Return only stable status codes; provider and Keychain diagnostics may contain secrets. */
    free(error);
    return ok;
}

unsigned int strappy_mutation_topics(const cJSON *command)
{
    const char *action = strappy_command_text(command, "action");
    if (!strcmp(action, "options") || !strcmp(action, "create_session")) return 1U;
    if (!strcmp(action, "include_context")) return 3U;
    if (!strcmp(action, "delete_session")) return 11U;
    if (!strcmp(action, "delete_study") || !strcmp(action, "reset_study")) return 15U;
    /* Account/model/catalog/default changes do not change conversation items. */
    return 4U;
}
