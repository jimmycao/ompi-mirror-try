#include "str_utils.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*  returns 1 iff str ends with suffix  */
int str_ends_with(const char * str, const char * suffix) {

  if( str == NULL || suffix == NULL )
    return 0;

  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);

  if (suffix_len > str_len)
    return 0;

  return 0 == strncmp(str + str_len - suffix_len, suffix, suffix_len);
}

/* concat args to command, return NULL if failed. */
char* concat_argv_to_cmd(const char** argv) {
    int total_len = 0;
    int offset = 0;
    while (argv[offset]) {
        total_len = total_len + strlen(argv[offset]) + 1;
        offset++;
    }
    if (total_len <= 0) {
        return NULL;
    }
    char* cmd = (char*)malloc(total_len + 1);
    cmd[0] = '\0';
    offset = 0;
    while (argv[offset]) {
        strcat(cmd, argv[offset]);
        strcat(cmd, " ");
        offset++;
    }
    return cmd;
}

/* env is key=val style, return NULL if failed. */
char* get_env_key(const char* env) {
    char* p_equal = strchr(env, '=');
    // not found
    if (!p_equal) {
        return NULL;
    }
    int key_len = p_equal - env;
    if (key_len <= 0) {
        return NULL;
    }
    char* key = (char*)malloc(key_len + 1);
    memcpy(key, env, key_len);
    key[key_len] = '\0';
    return key;
}

/* env is key=val style, return NULL if failed. */
char* get_env_val(const char* env) {
    char* p_equal = strchr(env, '=');
    // not found
    if (!p_equal) {
        return NULL;
    }
    int val_len = strlen(env) - (p_equal - env) - 1;
    char* val = (char*)malloc(val_len + 1);
    memcpy(val, p_equal + 1, val_len);
    val[val_len] = '\0';
    return val;
}