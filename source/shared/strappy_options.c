#include "strappy_options.h"

#include "strappy_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *strappy_web_provider_name(strappy_web_provider provider)
{
  switch (provider) {
    case STRAPPY_WEB_PROVIDER_NONE:
      return "none";
    case STRAPPY_WEB_PROVIDER_AUTO:
      return "auto";
    case STRAPPY_WEB_PROVIDER_NATIVE:
      return "native";
    case STRAPPY_WEB_PROVIDER_EXA:
      return "exa";
    case STRAPPY_WEB_PROVIDER_PARALLEL:
      return "parallel";
  }
  return NULL;
}

int strappy_web_provider_parse(const char *name,
                               strappy_web_provider *provider_out)
{
  strappy_web_provider provider;

  if ((name == NULL) || (provider_out == NULL)) {
    return 0;
  }
  if (strcmp(name, "none") == 0) {
    provider = STRAPPY_WEB_PROVIDER_NONE;
  } else if (strcmp(name, "auto") == 0) {
    provider = STRAPPY_WEB_PROVIDER_AUTO;
  } else if (strcmp(name, "native") == 0) {
    provider = STRAPPY_WEB_PROVIDER_NATIVE;
  } else if (strcmp(name, "exa") == 0) {
    provider = STRAPPY_WEB_PROVIDER_EXA;
  } else if (strcmp(name, "parallel") == 0) {
    provider = STRAPPY_WEB_PROVIDER_PARALLEL;
  } else {
    return 0;
  }
  *provider_out = provider;
  return 1;
}

int strappy_web_provider_is_enabled(strappy_web_provider provider)
{
  return (provider == STRAPPY_WEB_PROVIDER_AUTO) ||
    (provider == STRAPPY_WEB_PROVIDER_NATIVE) ||
    (provider == STRAPPY_WEB_PROVIDER_EXA) ||
    (provider == STRAPPY_WEB_PROVIDER_PARALLEL);
}
