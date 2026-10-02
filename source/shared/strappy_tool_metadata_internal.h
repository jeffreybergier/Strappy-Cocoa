#ifndef STRAPPY_TOOL_METADATA_INTERNAL_H
#define STRAPPY_TOOL_METADATA_INTERNAL_H
#include <cJSON.h>
char *strappy_tools_read_file(const char *path, char **error_out);
cJSON *strappy_tools_read_json_resource(const char *resource_dir,
                                               const char *filename,
                                               char **error_out);
char *strappy_tools_resource_path(const char *resource_dir,
                                         const char *filename,
                                         char **error_out);
#endif
