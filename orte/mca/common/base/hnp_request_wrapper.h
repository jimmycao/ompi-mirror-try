#ifndef HNP_REQUEST_WRAPPER_H_
#define HNP_REQUEST_WRAPPER_H_

#include "common.h"
#include "constants.h"

typedef enum {
	ALLOCATE,
	LAUNCH,
	REGISTER,
	FINISH,
	HEARTBEAT
} msg_type_enum_t;

extern void build_hnp_request_wrapper(avro_slice_t *inner_slice, msg_type_enum_t msg_type, avro_slice_t **wrapper_slice);

#endif /* HNP_REQUEST_WRAPPER_H_ */
