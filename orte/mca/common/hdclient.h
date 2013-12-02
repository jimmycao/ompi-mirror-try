#ifndef _ORTE_MCA_RPC_CLIENT_H
#define _ORTE_MCA_RPC_CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include "opal/util/output.h"
#include "base/allocate.h"
#include "base/heartbeat.h"
#include "base/launch.h"

extern int orte_umbilical_socket_id; 

int orte_hdclient_send(char* buffer, long length);

int orte_hdclient_recv(char** buffer, int* length);

/* interfaces */

extern int orte_hdclient_connect_to_am();

extern int register_to_am();

extern int allocate_resources_with_am(int resource_count, node_resource_t **node_resource_array, int *array_size);

extern int heartbeat_with_am(completed_proc_t **completed_proc_array, int *array_size);

extern int launch_procs_with_am(launch_context_t *launch_context_array, int request_array_size,
		launch_response_t **launch_response_array, int *response_array_size);


extern int finish_with_am(bool succeed, char *diagnostics);


#endif




