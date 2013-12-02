#ifndef HEARTBEAT_H_
#define HEARTBEAT_H_

#include "common.h"
#include "constants.h"

typedef enum {
	RUNNING,
	COMPLETED
} process_state_enum_t;

typedef struct {
	process_name_t proc_name;
	process_state_enum_t proc_state;
	int exit_value;
} completed_proc_t;

extern void build_heartbeat_request(avro_slice_t **slice);
extern void parse_heartbeat_response(avro_slice_t *slice, completed_proc_t **completed_proc_array, int *array_size);
extern void free_completed_proc_array(completed_proc_t *completed_proc_array);

#endif /* HEARTBEAT_H_ */
