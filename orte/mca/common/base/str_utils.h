#ifndef _HD_CLIENT_STR_UTILS_H
#define _HD_CLIENT_STR_UTILS_H

int str_ends_with(const char * str, const char * suffix);

/* concat args to command, return NULL if failed. */
char* concat_argv_to_cmd(const char** argv);

/* env is key=val style, return NULL if failed. */
char* get_env_key(const char* env);

/* env is key=val style, return NULL if failed. */
char* get_env_val(const char* env);

#endif