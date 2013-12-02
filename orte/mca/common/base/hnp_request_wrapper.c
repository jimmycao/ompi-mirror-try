#include "hnp_request_wrapper.h"
/*
{
	"type": "record",
	"name": "HamsterHnpRequest",
	"fields": [
		{"name": "request",  "type": "bytes"},
		{"name": "msg_type", "type": {"type": "enum",
  									  "name": "MsgType",
  									  "symbols" : ["ALLOCATE", "LAUNCH", "REGISTER", "FINISH", "HEARTBEAT"]
								      }
		}
	]
}
 */

extern void build_hnp_request_wrapper(avro_slice_t *inner_slice, msg_type_enum_t msg_type, avro_slice_t **wrapper_slice)
{
	char filename[FILE_NAME_LEN];
	char buf[BUFFER_SIZE];
	long len = 0;
	avro_schema_t schema;
	avro_value_iface_t *iface;
	avro_value_t record;
	avro_value_t request_value;
	avro_value_t msg_type_value;
	size_t index;
	avro_writer_t writer;

	sprintf(filename, "%s/%s", avro_schema_path, "HamsterHnpRequestRecordAvro.avsc");
	init_schema(filename, &schema);

	iface = avro_generic_class_from_schema(schema);
	avro_generic_value_new(iface, &record);

	avro_value_get_by_name(&record, "request", &request_value, &index);
	avro_value_set_bytes(&request_value, inner_slice->buffer, inner_slice->len);

	avro_value_get_by_name(&record, "msg_type", &msg_type_value, &index);
	avro_value_set_enum(&msg_type_value, msg_type);

	writer = avro_writer_memory(buf, sizeof(buf));
	/* write record to writer (buffer) */
	if (avro_value_write(writer, &record)) {
		fprintf(stderr, "build_hnp_request_wrapper: Unable to write record to memory buffer\n");
		fprintf(stderr, "Error: %s\n", avro_strerror());
		exit(1);
	}

	avro_writer_flush(writer);
	len = avro_writer_tell(writer);

	//avro_generic_value_free(&record);
	avro_value_iface_decref(iface);
	avro_schema_decref(schema);

	*wrapper_slice = xmalloc(sizeof(avro_slice_t));
	(*wrapper_slice)->buffer = xmalloc(len);
	(*wrapper_slice)->len = len;
	memcpy((*wrapper_slice)->buffer, buf, len);
}
