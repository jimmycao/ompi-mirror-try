#include "finish.h"

extern void build_finish_request(bool succeed, char *diagnostics, avro_slice_t **slice)
{
	char filename[FILE_NAME_LEN];
	char buf[BUFFER_SIZE];
	long len = 0;
	avro_schema_t schema;
	avro_value_iface_t *iface;
	avro_value_t record;
	avro_value_t succeed_value, diagnostics_value;
	size_t index;
	avro_writer_t writer;

	sprintf(filename, "%s/%s", avro_schema_path, "FinishRequestRecordAvro.avsc");
	init_schema(filename, &schema);

	iface = avro_generic_class_from_schema(schema);
	avro_generic_value_new(iface, &record);

	avro_value_get_by_name(&record, "succeed", &succeed_value, &index);
	avro_value_set_boolean(&succeed_value, succeed);

	avro_value_get_by_name(&record, "diagnostics", &diagnostics_value, &index);
	avro_value_set_string(&diagnostics_value, diagnostics);

	/* create a writer with memory buffer */
	writer = avro_writer_memory(buf, sizeof(buf));
	/* write record to writer (buffer) */
	if (avro_value_write(writer, &record)) {
		fprintf(stderr, "build_finish_request: Unable to write record to memory buffer\n");
		fprintf(stderr, "Error: %s\n", avro_strerror());
		exit(1);
	}

	avro_writer_flush(writer);
	len = avro_writer_tell(writer);

	//avro_generic_value_free(&record);
	avro_value_iface_decref(iface);
	avro_schema_decref(schema);

	*slice = xmalloc(sizeof(avro_slice_t));
	(*slice)->buffer = xmalloc(len);
	(*slice)->len = len;
	memcpy((*slice)->buffer, buf, len);
}

extern int parse_finish_response(avro_slice_t *slice)
{
	char filename[FILE_NAME_LEN];
	avro_schema_t schema;
	avro_value_iface_t *iface;
	avro_value_t record, finish_response_value;
	size_t index;
	avro_reader_t reader;
	int rc;
	avro_type_t type;

	sprintf(filename, "%s/%s", avro_schema_path, "FinishResponseRecordAvro.avsc");
	init_schema(filename, &schema);

	iface = avro_generic_class_from_schema(schema);
	avro_generic_value_new(iface, &record);

	reader = avro_reader_memory(slice->buffer, slice->len);
	if (avro_value_read(reader, &record)) {
		fprintf(stderr, "parse_finish_response: Unable to read record from memory buffer\n");
		fprintf(stderr, "Error: %s\n", avro_strerror());
		exit(1);
	}

	avro_value_get_by_name(&record, "finish_response", &finish_response_value, &index);
	rc = avro_value_get_null(&finish_response_value);

	type = avro_value_get_type(&finish_response_value);

	avro_value_iface_decref(iface);
	avro_schema_decref(schema);

	if (slice->len == 0 && rc == 0 && type == AVRO_NULL) {
		return 0;
	} else {
		return -1;
	}
}
