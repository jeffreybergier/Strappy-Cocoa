#ifndef STRAPPY_IDENTITY_H
#define STRAPPY_IDENTITY_H

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_IDENTITY_PRODUCT_NAME "Strappy"

char *strappy_identity_copy_user_agent(char **error_out);

#ifdef __cplusplus
}
#endif

#endif
