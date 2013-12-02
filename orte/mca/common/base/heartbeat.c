#include "heartbeat.h"

extern void build_heartbeat_request(avro_slice_t **slice)
{
	char filename[FILE_NAME_LEN];
	char buf[BUFFER_SIZE];
	long len = 0;
	avro_schema_t schema;
	avro_value_iface_t *iface;
	avro_value_t record, heartbeat_request_value;
	avro_writer_t writer;
	size_t index;


	sprintf(filename, "%s/%s", avro_schema_path, "HeartBeatRequestRecordAvro.avsc");
	init_schema(filename, &schema);

	iface = avro_generic_class_from_schema(schema);
	avro_generic_value_new(iface, &record);

	avro_value_get_by_name(&record, "heartbeat_request", &heartbeat_request_value, &index);
	avro_value_set_null(&heartbeat_request_value);

	/* create a writer with memory buffer */
	writer = avro_writer_memory(buf, sizeof(buf));
	/* write record to writer (buffer) */
	if (avro_value_write(writer, &record)) {
		fprintf(stderr, "build_heartbeat_request: Unable to write person record to memory buffer\n");
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

extern void parse_heartbeat_response(avro_slice_t *slice, completed_proc_t **completed_proc_array, int *array_size)
{
	char filename[FILE_NAME_LEN];
	avro_schema_t schema;
	avro_value_iface_t *iface;
	avro_reader_t reader;
	avro_value_t record;
	size_t index;
	avro_value_t completed_processes_value, ProcessStatus_value;
	avro_value_t name_value, jobid_value, vpid_value;
	avro_value_t state_value;
	avro_value_t exit_value_value;
	int i;

	sprintf(filename, "%s/%s", avro_schema_path, "HeartBeatResponseRecordAvro.avsc");
	init_schema(filename, &schema);

	iface = avro_generic_class_from_schema(schema);
	avro_generic_value_new(iface, &record);

	reader = avro_reader_memory(slice->buffer, slice->len);
	if (avro_value_read(reader, &record)) {
		fprintf(stderr, "build_heartbeat_request: Unable to read record from memory buffer\n");
		fprintf(stderr, "Error: %s\n", avro_strerror());
		exit(1);
	}

	avro_value_get_by_name(&record, "completed_processes", &completed_processes_value, &index);
	avro_value_get_size(&completed_processes_value, array_size);

	if (0 == *array_size) {
		*completed_proc_array = NULL;
	} else {
		*completed_proc_array = xmalloc(sizeof(completed_proc_t) * (*array_size));
		for (i = 0; i < *array_size; i++) {
			size_t size;
			avro_value_get_by_index(&completed_processes_value, i, &ProcessStatus_value, NULL);

			avro_value_get_by_name(&ProcessStatus_value, "name", &name_value, &index);
			avro_value_get_by_name(&name_value, "jobid", &jobid_value, &index);
			avro_value_get_int(&jobid_value, &((*completed_proc_array)[i].proc_name.jobid));
			avro_value_get_by_name(&name_value, "vpid", &vpid_value, &index);
			avro_value_get_int(&vpid_value, &((*completed_proc_array)[i].proc_name.vpid));

			avro_value_get_by_name(&ProcessStatus_value, "state", &state_value, &index);
			avro_value_get_enum(&state_value, &((*completed_proc_array)[i].proc_state));

			avro_value_get_by_name(&ProcessStatus_value, "exit_value", &exit_value_value, &index);
			avro_value_get_int(&exit_value_value, &((*completed_proc_array)[i].exit_value));
		}
	}

	//avro_generic_value_free(&record);
	avro_value_iface_decref(iface);
	avro_schema_decref(schema);
}

extern void free_completed_proc_array(completed_proc_t *completed_proc_array)
{
	if (completed_proc_array) {
		free(completed_proc_array);
	}
}
