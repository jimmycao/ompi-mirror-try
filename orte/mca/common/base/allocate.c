#include "allocate.h"

extern void build_allocate_request(int resource_count, avro_slice_t **slice)
{
	char filename[FILE_NAME_LEN];
	char buf[BUFFER_SIZE];
	long len = 0;
	avro_schema_t schema;
	avro_value_iface_t *iface;
	avro_value_t record;
	avro_value_t resource_count_value;
	size_t index;
	avro_writer_t writer;

	sprintf(filename, "%s/%s", avro_schema_path, "AllocateRequestRecordAvro.avsc");
	init_schema(filename, &schema);

	iface = avro_generic_class_from_schema(schema);
	avro_generic_value_new(iface, &record);

	avro_value_get_by_name(&record, "resource_count", &resource_count_value, &index);
	avro_value_set_int(&resource_count_value, resource_count);

	/* create a writer with memory buffer */
	writer = avro_writer_memory(buf, sizeof(buf));
	/* write record to writer (buffer) */
	if (avro_value_write(writer, &record)) {
		fprintf(stderr, "build_allocate_request: Unable to write record to memory buffer\n");
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

extern void parse_allocate_response(avro_slice_t *slice, node_resource_t **node_resource_array, int *array_size)
{
	char filename[FILE_NAME_LEN];
	avro_schema_t schema;
	avro_value_iface_t *iface;
	avro_reader_t reader;
	avro_value_t record;
	size_t index;
	avro_value_t node_resources_value, NodeResource_value, host_name_value, slot_value;
	int i;

	sprintf(filename, "%s/%s", avro_schema_path, "AllocateResponseRecordAvro.avsc");
	init_schema(filename, &schema);

	iface = avro_generic_class_from_schema(schema);
	avro_generic_value_new(iface, &record);

	reader = avro_reader_memory(slice->buffer, slice->len);
	if (avro_value_read(reader, &record)) {
		fprintf(stderr, "parse_allocate_response: Unable to read record from memory buffer\n");
		fprintf(stderr, "Error: %s\n", avro_strerror());
		exit(1);
	}

	avro_value_get_by_name(&record, "node_resources", &node_resources_value, &index);
	avro_value_get_size(&node_resources_value, array_size);
	if (0 == *array_size) {
		*node_resource_array = NULL;
	} else {
		*node_resource_array = xmalloc(sizeof(node_resource_t) * (*array_size));
		for (i = 0; i < *array_size; i++) {
			size_t size;
			int slot_num = 0;
			avro_value_get_by_index(&node_resources_value, i, &NodeResource_value, NULL);
			avro_value_get_by_name(&NodeResource_value, "host_name", &host_name_value, &index);
			avro_value_get_string(&host_name_value, &((*node_resource_array)[i].host_name), &size);

			avro_value_get_by_name(&NodeResource_value, "slot", &slot_value, &index);
			avro_value_get_int(&slot_value, &slot_num);
			(*node_resource_array)[i].slot_num = slot_num;
		}
	}

	//avro_generic_value_free(&record);
	avro_value_iface_decref(iface);
	avro_schema_decref(schema);
}


extern void free_node_resource_array(node_resource_t *node_resource_array, int array_size)
{
	int i;
	for (i = 0; i < array_size; i++) {
		if (node_resource_array[i].host_name) {
			free(node_resource_array[i].host_name);
		}
	}

	if (node_resource_array) {
		free(node_resource_array);
	}
}
