#include "strappy_identity.h"

#include "strappy_cocoa.h"
#include "strappy_core.h"

#include <stdlib.h>
#include <string.h>

static int strappy_identity_is_http_token_character(unsigned char character)
{
  if (((character >= (unsigned char)'0') &&
       (character <= (unsigned char)'9')) ||
      ((character >= (unsigned char)'A') &&
       (character <= (unsigned char)'Z')) ||
      ((character >= (unsigned char)'a') &&
       (character <= (unsigned char)'z'))) {
    return 1;
  }

  return (strchr("!#$%&'*+-.^_`|~", (int)character) != NULL) ? 1 : 0;
}

static int strappy_identity_version_is_valid(const char *version)
{
  const unsigned char *cursor;

  if ((version == NULL) || (version[0] == '\0')) {
    return 0;
  }
  cursor = (const unsigned char *)version;
  while (*cursor != '\0') {
    if (!strappy_identity_is_http_token_character(*cursor)) {
      return 0;
    }
    cursor++;
  }
  return 1;
}

char *strappy_identity_copy_user_agent(char **error_out)
{
  char *user_agent;
  char *version;
  size_t product_length;
  size_t version_length;

  version = strappy_cocoa_copy_app_version(error_out);
  if (version == NULL) {
    return NULL;
  }
  if (!strappy_identity_version_is_valid(version)) {
    free(version);
    strappy_set_error(error_out,
                      "CFBundleShortVersionString is not a valid HTTP "
                      "product version.");
    return NULL;
  }

  product_length = strlen(STRAPPY_IDENTITY_PRODUCT_NAME);
  version_length = strlen(version);
  if (product_length > (((size_t)-1) - version_length - 2U)) {
    free(version);
    strappy_set_error(error_out, "The Strappy user agent is too large.");
    return NULL;
  }

  user_agent = (char *)malloc(product_length + version_length + 2U);
  if (user_agent == NULL) {
    free(version);
    strappy_set_error(error_out, "Could not allocate the Strappy user agent.");
    return NULL;
  }
  memcpy(user_agent, STRAPPY_IDENTITY_PRODUCT_NAME, product_length);
  user_agent[product_length] = '/';
  memcpy(user_agent + product_length + 1U, version, version_length + 1U);
  free(version);
  return user_agent;
}
