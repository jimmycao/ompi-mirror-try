#ifndef COMMON_H_
#define COMMON_H_

#include <avro.h>

typedef struct avro_slice {
	void *buffer;
	int len;
} avro_slice_t;

typedef struct {
	int jobid;
	int vpid;
} process_name_t;

extern void init_schema(char *filename, avro_schema_t *schema);

//extern void init_schema_from_json(char *filename, avro_schema_t *schema);

extern void read_file_to_slice(char *filename, avro_slice_t **slice);

extern void write_slice_to_file(char *filename, avro_slice_t *slice);

extern void free_slice(avro_slice_t *slice);

extern void *xmalloc(size_t size);


#endif /* COMMON_H_ */
