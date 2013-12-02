#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include <avro.h>

extern void read_file_to_slice(char *filename, avro_slice_t **slice)
{
	FILE *fp;

	if (NULL == (fp = fopen(filename, "r"))) {
		fprintf(stderr, "%s : Open file failed.\n", filename);
		exit(1);
	}

	fseek(fp, 0, SEEK_END);
	*slice = xmalloc(sizeof(avro_slice_t));
	(*slice)->len = ftell(fp);
	rewind(fp);
	(*slice)->buffer = xmalloc((*slice)->len);
	fread((*slice)->buffer, 1, (*slice)->len, fp);
	fclose(fp);
}


/**
 *  init schema with a schema file
 */
extern void init_schema(char *filename, avro_schema_t *schema)
{
	avro_slice_t *schema_slice;
	read_file_to_slice(filename, &schema_slice);
	if (avro_schema_from_json_length(schema_slice->buffer, schema_slice->len, schema)) {
		fprintf(stderr, "Unable to parse schema. \n");
		fprintf(stderr, "Error: %s\n", avro_strerror());
		exit(1);
	}
	free_slice(schema_slice);
}
///*
//{
//	"type": "record",
//	"name": "RegisterRequest",
//	"fields": []
//}
// */
//extern void init_schema_from_json(char *filename, avro_schema_t *schema)
//{
//	avro_slice_t *slice;
//	read_file_to_slice(filename, &slice);
//	if (avro_schema_from_json(slice->buffer, 0, schema, NULL)) {
//		fprintf(stderr, "Unable to parse schema. \n");
//		fprintf(stderr, "Error : %s\n", avro_strerror());
//		exit(1);
//	}
//	free_slice(slice);
//}

extern void write_slice_to_file(char *filename, avro_slice_t *slice)
{
	FILE *fp;

	remove(filename);
	if (NULL == (fp = fopen(filename, "w"))) {
		fprintf(stderr, "%s : Open file failed.\n", filename);
		exit(1);
	}

	fwrite(slice->buffer, 1, slice->len, fp);
	fclose(fp);
}


extern void free_slice(avro_slice_t *slice)
{
	if (slice->buffer) {
		free(slice->buffer);
	}
	if (slice) {
		free(slice);
	}
}

extern void *xmalloc(size_t size)
{
	void *p = NULL;
	if (!(p = malloc(size))) {
		fprintf(stderr, "malloc failed.\n");
		exit(1);
	}
	return p;
}
