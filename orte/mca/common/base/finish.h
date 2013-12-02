#ifndef FINISH_H_
#define FINISH_H_

#include "common.h"
#include "constants.h"


extern void build_finish_request(bool succeed, char *diagnostics, avro_slice_t **slice);
extern int parse_finish_response(avro_slice_t *slice);



#endif /* FINISH_H_ */
