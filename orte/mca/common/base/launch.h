#ifndef LAUNCH_H_
#define LAUNCH_H_

#include "constants.h"
#include "common.h"


typedef struct {
	char **en_vars_array;  /* ended with NULL */
	char *args;
	char *host_name;
	process_name_t proc_name;
} launch_context_t;

typedef struct {
	bool is_successful;
	process_name_t proc_name;
} launch_response_t;


extern void build_launch_request(launch_context_t *launch_context_array, int array_size, avro_slice_t **slice);

extern void parse_launch_response(avro_slice_t *slice, launch_response_t **launch_response_array, int *array_size);

extern void free_launch_context_array(launch_context_t *launch_context_array, int array_size);

extern void free_launch_response_array(launch_response_t *launch_response_array);


#endif /* LAUNCH_H_ */
