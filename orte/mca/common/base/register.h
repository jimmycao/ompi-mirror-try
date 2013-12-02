#ifndef REGISTER_REQUEST_H_
#define REGISTER_REQUEST_H_

#include "common.h"
#include "constants.h"

extern void build_register_request(avro_slice_t **slice);
extern int parse_register_response(avro_slice_t *slice);

#endif /* REGISTER_REQUEST_H_ */
