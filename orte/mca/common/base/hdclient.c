#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "../hdclient.h"
#include "constants.h"
#include "str_utils.h"
#include "net_utils.h"


#include "register.h"
#include "hnp_request_wrapper.h"
#include "allocate.h"
#include "finish.h"

#define HANDSHAKE "hamster-001"


int orte_umbilical_socket_id = -1;

static int connect_to_server(int socket_id, const char* host, int port);


/* connect_to_server, return 0 if succeed */
static int connect_to_server(int socket_id, const char* host, int port)
{
    //define socket variables
    struct sockaddr_in serv_addr;
    struct hostent *server;

    //init port / socket / server
    server = gethostbyname(host);
    if (server == NULL)
    {
        opal_output(0, "ERROR, no such host.\n");
        return -1;
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
          (char *)&serv_addr.sin_addr.s_addr,
                server->h_length);
                serv_addr.sin_port = htons(port);

    //connect via socket
    if (connect(socket_id, &serv_addr, sizeof(serv_addr)) < 0)
    {
        opal_output(0, "ERROR connecting.\n");
        return -1;
    }

    return 0;
}

int orte_hdclient_connect_to_am()
{
    int rc;

    // init socket
    orte_umbilical_socket_id = socket(AF_INET, SOCK_STREAM, 0);

    // read host/port
    char host[1024];
    rc = gethostname(host, 1023);
    char* port_str = getenv(AM_UMBILICAL_PORT_ENV_KEY);
    if (!port_str || (0 != rc)) {
        opal_output(0, "failed to get host or port when trying to connect to AM");
        return -1;
    }
    int port = atoi(port_str);

    // connect to server
    rc = connect_to_server(orte_umbilical_socket_id, host, port);
    if (0 != rc) {
        opal_output(0, "connect to AM failed.");
        return -1;
    }

    // send handshake
    rc = write_all(orte_umbilical_socket_id, HANDSHAKE, strlen(HANDSHAKE));
    if (0 != rc) {
        opal_output(0, "send handshake to socket failed");
        return -1;
    }

    // recv handshake
    char recv[strlen(HANDSHAKE) + 1];
    rc = read_all(orte_umbilical_socket_id, recv, strlen(HANDSHAKE));
    if (0 != rc) {
        opal_output(0, "read handshake from socket failed");
        return -1;
    }
    recv[strlen(HANDSHAKE)] = '\0';

    // valid handshake recved from AM
    if (0 != strcmp(HANDSHAKE, recv)) {
        opal_output(0, "failed to validate handshake from AM, read str is %s", recv);
        return -1;
    }

    return 0;
}

int orte_hdclient_send(char* buffer, long length)
{
	int rc;
	// insanity check
	if (!buffer) {
		opal_output(0, "a NULL buffer passed in, what happened?");
		return -1;
	}

	// pack message and send
	write_endian_swap_int(orte_umbilical_socket_id, length);
	rc = write_all(orte_umbilical_socket_id, buffer, length);
	if (rc != 0) {
		opal_output(0, "failed to send buffer to AM");
		return -1;
	}

	return 0;
}


int orte_hdclient_recv(char** buffer, int* length)
{
    int rc;
    int8_t success;
    int len = 0;

    rc = read_all(orte_umbilical_socket_id, &success, 1);
    if (0 != rc) {
        opal_output(0, "read success status from socket failed");
        return -1;
    }
    if (success == 2) {
        opal_output(0, "recved error response from AM");
        return -1;
    } else if (success != 1) {
        opal_output(0, "recved unknown response from AM");
        return -1;
    }

    // read len from socket
    rc = read_all(orte_umbilical_socket_id, &len, sizeof(int));
    if (0 != rc) {
        opal_output(0, "read length of umbilical socket failed.");
        return -1;
    }
    len = int_endian_swap(len);

    // create buffer and read buffer
    *buffer = (char*)malloc(len);
    if (!*buffer) {
        opal_output(0, "failed to allocate memory for recv message");
        return -1;
    }
    rc = read_all(orte_umbilical_socket_id, *buffer, len);
    if (rc != 0) {
        opal_output(0, "failed to read response message from AM");
        free(*buffer);
        return -1;
    }

    *length = len;
    return 0;
}

/*
extern void build_register_request(avro_slice_t **slice);
extern int parse_register_response(avro_slice_t *slice);
build_hnp_request_wrapper(avro_slice_t *inner_slice, msg_type_enum_t msg_type, avro_slice_t **wrapper_slice);
 */
extern int register_to_am()
{
	avro_slice_t *request_slice;
	avro_slice_t *request_slice_wrappered;

	/* send register request */
	build_register_request(&request_slice);
	build_hnp_request_wrapper(request_slice, REGISTER, &request_slice_wrappered);

	if (0 != orte_hdclient_send(request_slice_wrappered->buffer, request_slice_wrappered->len)) {
		opal_output(0, "register_to_am: error happened when send register request to AM");
		free_slice(request_slice_wrappered);
		free_slice(request_slice);
		return -1;
	}

	free_slice(request_slice_wrappered);
	free_slice(request_slice);

	/* recv register response */
	char *buffer = NULL;
	int length = 0;
	avro_slice_t response_slice;

	if (0 != orte_hdclient_recv(&buffer, &length)) {
		opal_output(0, "register_to_am: error happened when recv register response from AM");
		goto cleanup;
	}

	response_slice.buffer = buffer;
	response_slice.len = length;
	if (0 != parse_register_response(&response_slice)) {
		opal_output(0, "register_to_am: error happened when parse register response");
		goto cleanup;
	}

	if (buffer) {
		free(buffer);
	}
	return 0;

cleanup:
	if (buffer) {
		free(buffer);
	}
	return -1;
}

/*
	typedef struct {
		char *host_name;
		int slot_num;
	} node_resource_t;

	extern void build_allocate_request(int resource_count, avro_slice_t **slice);
	extern void parse_allocate_response(avro_slice_t *slice, node_resource_t **node_resource_array, int *array_size);

	extern void free_node_resource_array(node_resource_t *node_resource_array, int array_size);

	build_hnp_request_wrapper(avro_slice_t *inner_slice, msg_type_enum_t msg_type, avro_slice_t **wrapper_slice);
 */
extern int allocate_resources_with_am(int resource_count, node_resource_t **node_resource_array, int *array_size)
{
	avro_slice_t *request_slice;
	avro_slice_t *request_slice_wrappered;

	/* send allocate request */
	build_allocate_request(resource_count, &request_slice);
	build_hnp_request_wrapper(request_slice, ALLOCATE, &request_slice_wrappered);

	if (0 != orte_hdclient_send(request_slice_wrappered->buffer, request_slice_wrappered->len)) {
		opal_output(0, "allocate_resources_from_am: error happened when send allocate request to AM");
		free_slice(request_slice_wrappered);
		free_slice(request_slice);
		return -1;
	}

	free_slice(request_slice_wrappered);
	free_slice(request_slice);

	/* recv allocate response */
	char *buffer = NULL;
	int length = 0;
	avro_slice_t response_slice;

	if (0 != orte_hdclient_recv(&buffer, &length)) {
		opal_output(0, "allocate_resources_from_am: error happened when recv allocate response from AM");
		goto cleanup;
	}

	response_slice.buffer = buffer;
	response_slice.len = length;

	parse_allocate_response(&response_slice, node_resource_array, array_size);

	if (buffer) {
		free(buffer);
	}
	return 0;

cleanup:
	if (buffer) {
		free(buffer);
	}
	return -1;
}


/*
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
 */

extern int heartbeat_with_am(completed_proc_t **completed_proc_array, int *array_size)
{
	avro_slice_t *request_slice;
	avro_slice_t *request_slice_wrappered;

	/* send heartbeat request */
	build_heartbeat_request(&request_slice);
	build_hnp_request_wrapper(request_slice, HEARTBEAT, &request_slice_wrappered);

	if (0 != orte_hdclient_send(request_slice_wrappered->buffer, request_slice_wrappered->len)) {
		opal_output(0, "heartbeat_with_am: error happened when send heartbeat request to AM");
		free_slice(request_slice_wrappered);
		free_slice(request_slice);
		return -1;
	}

	free_slice(request_slice_wrappered);
	free_slice(request_slice);

	/* recv heartbeat response */
	char *buffer = NULL;
	int length = 0;
	avro_slice_t response_slice;

	if (0 != orte_hdclient_recv(&buffer, &length)) {
		opal_output(0, "heartbeat_with_am: error happened when recv heartbeat response from AM");
		goto cleanup;
	}

	response_slice.buffer = buffer;
	response_slice.len = length;

	parse_heartbeat_response(&response_slice, completed_proc_array, array_size);

	if (buffer) {
		free(buffer);
	}
	return 0;

cleanup:
	if (buffer) {
		free(buffer);
	}
	return -1;
}

/*
typedef struct {
	char *en_vars;
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
*/

extern int launch_procs_with_am(launch_context_t *launch_context_array, int request_array_size,
		launch_response_t **launch_response_array, int *response_array_size)
{
	avro_slice_t *request_slice;
	avro_slice_t *request_slice_wrappered;

	/* send launch request */
	build_launch_request(launch_context_array, request_array_size, &request_slice);
	build_hnp_request_wrapper(request_slice, LAUNCH, &request_slice_wrappered);

	if (0 != orte_hdclient_send(request_slice_wrappered->buffer, request_slice_wrappered->len)) {
		opal_output(0, "launch_procs_with_am: error happened when send launch request to AM");
		free_slice(request_slice_wrappered);
		free_slice(request_slice);
		return -1;
	}

	free_slice(request_slice_wrappered);
	free_slice(request_slice);

	/* recv launch response */
	char *buffer = NULL;
	int length = 0;
	avro_slice_t response_slice;

	if (0 != orte_hdclient_recv(&buffer, &length)) {
		opal_output(0, "launch_procs_with_am: error happened when recv launch response from AM");
		goto cleanup;
	}

	response_slice.buffer = buffer;
	response_slice.len = length;

	parse_launch_response(&response_slice, launch_response_array, response_array_size);

	if (buffer) {
		free(buffer);
	}
	return 0;

cleanup:
	if (buffer) {
		free(buffer);
	}
	return -1;
}


//extern void build_finish_request(bool succeed, char *diagnostics, avro_slice_t **slice);
//extern int parse_finish_response(avro_slice_t *slice);

extern int finish_with_am(bool succeed, char *diagnostics)
{
	avro_slice_t *request_slice;
	avro_slice_t *request_slice_wrappered;

	/* send finish request */
	build_finish_request(succeed, diagnostics, &request_slice);
	build_hnp_request_wrapper(request_slice, FINISH, &request_slice_wrappered);

	if (0 != orte_hdclient_send(request_slice_wrappered->buffer, request_slice_wrappered->len)) {
		opal_output(0, "finish_with_am: error happened when send finish request to AM");
		free_slice(request_slice_wrappered);
		free_slice(request_slice);
		return -1;
	}

	free_slice(request_slice_wrappered);
	free_slice(request_slice);

	/* recv finish response */
	char *buffer = NULL;
	int length = 0;
	avro_slice_t response_slice;
	int rc;

	if (0 != orte_hdclient_recv(&buffer, &length)) {
		opal_output(0, "finish_with_am: error happened when recv finish response from AM");
		goto cleanup;
	}

	response_slice.buffer = buffer;
	response_slice.len = length;

	rc = parse_finish_response(&response_slice);
	if (0 != rc) {
		opal_output(0, "finish_with_am: error happened when parse finish response from AM");
		goto cleanup;
	}

	if (buffer) {
		free(buffer);
	}
	return 0;

cleanup:
	if (buffer) {
		free(buffer);
	}
	return -1;
}
