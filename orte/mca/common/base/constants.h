#ifndef CONSTANTS_H_
#define CONSTANTS_H_

#if HAVE_STDBOOL_H
#  include <stdbool.h>
#else
typedef enum {false, true} bool;
#endif /* !HAVE_STDBOOL_H */

#define AM_UMBILICAL_PORT_ENV_KEY "AM_UMBILICAL_PORT"

//#define SCHEMA_PATH "/Users/caoj7/workspace/hamster-avro/schema"

#define AVRO_SCHEMA_PATH_ENV_KEY "AVRO_SCHEMA_PATH"

extern char *avro_schema_path;

#define FILE_NAME_LEN 256
#define BUFFER_SIZE   65536*8


#endif /* CONSTANTS_H_ */
