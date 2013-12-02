#include "launch.h"

extern void build_launch_request(launch_context_t *launch_context_array, int array_size, avro_slice_t **slice)
{
	char filename[FILE_NAME_LEN];
	char buf[BUFFER_SIZE];
	long len = 0;
	avro_schema_t schema;
	avro_value_iface_t *iface;
	avro_value_t record;
	avro_value_t launchContexts_value, launchContext_value, en_vars_array_value, en_var_item_value, args_value, host_name_value;
	avro_value_t name_value, jobid_value, vpid_value;
	size_t index;
	avro_writer_t writer;
	int i;

	sprintf(filename, "%s/%s", avro_schema_path, "LaunchRequestRecordAvro.avsc");
	init_schema(filename, &schema);

	iface = avro_generic_class_from_schema(schema);
	avro_generic_value_new(iface, &record);

	avro_value_get_by_name(&record, "launchContexts", &launchContexts_value, &index);

	for (i = 0; i < array_size; i++) {
		avro_value_append(&launchContexts_value, &launchContext_value, &index);

		avro_value_get_by_name(&launchContext_value, "en_vars", &en_vars_array_value, &index);
		while (*(launch_context_array[i].en_vars_array)) {
			avro_value_append(&en_vars_array_value, &en_var_item_value, &index);
			avro_value_set_string(&en_var_item_value, *(launch_context_array[i].en_vars_array));
			launch_context_array[i].en_vars_array++;
		}

		avro_value_get_by_name(&launchContext_value, "args", &args_value, &index);
		avro_value_set_string(&args_value, launch_context_array[i].args);

		avro_value_get_by_name(&launchContext_value, "host_name", &host_name_value, &index);
		avro_value_set_string(&host_name_value, launch_context_array[i].host_name);

		avro_value_get_by_name(&launchContext_value, "name", &name_value, &index);

		avro_value_get_by_name(&name_value, "jobid", &jobid_value, &index);
		avro_value_set_int(&jobid_value, launch_context_array[i].proc_name.jobid);

		avro_value_get_by_name(&name_value, "vpid", &vpid_value, &index);
		avro_value_set_int(&vpid_value, launch_context_array[i].proc_name.vpid);
	}

	/* create a writer with memory buffer */
	writer = avro_writer_memory(buf, sizeof(buf));
	/* write record to writer (buffer) */
	if (avro_value_write(writer, &record)) {
		fprintf(stderr, "build_launch_request: Unable to write record to memory buffer\n");
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



extern void free_launch_context_array(launch_context_t *launch_context_array, int array_size)
{
	int i;
	for (i = 0; i < array_size; i++) {
		while (*(launch_context_array[i].en_vars_array)) {
			free(*(launch_context_array[i].en_vars_array));
			launch_context_array[i].en_vars_array++;
		}

		if (launch_context_array[i].args) {
			free(launch_context_array[i].args);
		}
		if (launch_context_array[i].host_name) {
			free(launch_context_array[i].host_name);
		}
	}

	if (launch_context_array) {
		free(launch_context_array);
	}
}

extern void parse_launch_response(avro_slice_t *slice, launch_response_t **launch_response_array, int *array_size)
{
	char filename[FILE_NAME_LEN];
	avro_schema_t schema;
	avro_value_iface_t *iface;
	avro_reader_t reader;
	avro_value_t record;
	size_t index;
	avro_value_t results_value, LaunchResult_value, is_successful_value, name_value, jobid_value, vpid_value;
	int i;

	sprintf(filename, "%s/%s", avro_schema_path, "LaunchResponseRecordAvro.avsc");
	init_schema(filename, &schema);

	iface = avro_generic_class_from_schema(schema);
	avro_generic_value_new(iface, &record);

	reader = avro_reader_memory(slice->buffer, slice->len);
	if (avro_value_read(reader, &record)) {
		fprintf(stderr, "build_launch_request: Unable to read record from memory buffer\n");
		fprintf(stderr, "Error: %s\n", avro_strerror());
		exit(1);
	}

	avro_value_get_by_name(&record, "results", &results_value, &index);
	avro_value_get_size(&results_value, array_size);

	if (0 == *array_size) {
		*launch_response_array = NULL;
	} else {
		*launch_response_array = xmalloc(sizeof(launch_response_t) * (*array_size));
		for (i = 0; i < *array_size; i++) {
			size_t size;
			avro_value_get_by_index(&results_value, i, &LaunchResult_value, NULL);

			avro_value_get_by_name(&LaunchResult_value, "is_successful", &is_successful_value, &index);
			avro_value_get_boolean(&is_successful_value, &((*launch_response_array)[i].is_successful));

			avro_value_get_by_name(&LaunchResult_value, "name", &name_value, &index);

			avro_value_get_by_name(&name_value, "jobid", &jobid_value, &index);
			avro_value_get_int(&jobid_value, &((*launch_response_array)[i].proc_name.jobid));

			avro_value_get_by_name(&name_value, "vpid", &vpid_value, &index);
			avro_value_get_int(&vpid_value, &(*launch_response_array)[i].proc_name.vpid);
		}
	}

	//avro_generic_value_free(&record);
	avro_value_iface_decref(iface);
	avro_schema_decref(schema);
}

extern void free_launch_response_array(launch_response_t *launch_response_array)
{
	if (launch_response_array) {
		free(launch_response_array);
	}
}
