/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2006 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2006-2007 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2007-2012 Los Alamos National Security, LLC.  All rights
 *                         reserved. 
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 *
 * These symbols are in a file by themselves to provide nice linker
 * semantics.  Since linkers generally pull in symbols by object
 * files, keeping these symbols as the only symbols in this file
 * prevents utility programs such as "ompi_info" from having to import
 * entire components just to query their version and parameters.
 */

#include "orte_config.h"

#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <signal.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include "opal/mca/installdirs/installdirs.h"
#include "opal/util/argv.h"
#include "opal/util/output.h"
#include "opal/util/opal_environ.h"
#include "opal/util/path.h"
#include "opal/util/basename.h"
#include "opal/mca/base/mca_base_var.h"


#include "orte/constants.h"
#include "orte/types.h"
#include "orte/util/show_help.h"
#include "orte/util/name_fns.h"
#include "orte/runtime/orte_globals.h"
#include "orte/runtime/orte_wait.h"
#include "orte/runtime/orte_quit.h"
#include "orte/mca/errmgr/errmgr.h"
#include "orte/mca/rmaps/rmaps.h"
#include "orte/mca/state/state.h"

#include "orte/orted/orted.h"

#include "orte/mca/plm/plm.h"
#include "orte/mca/plm/base/plm_private.h"
#include "plm_yarn.h"

#include "orte/mca/rml/rml.h"
#include "orte/mca/grpcomm/grpcomm.h"
#include "orte/mca/ess/base/base.h"
#include "orte/mca/plm/base/base.h"
#include "orte/mca/plm/plm_types.h"

#include "orte/mca/common/hdclient.h"
#include "orte/mca/common/base/heartbeat.h"
#include "orte/mca/common/base/launch.h"


#define ORTE_RML_TAG_YARN_SYNC_REQUEST      97
#define ORTE_RML_TAG_YARN_SYNC_RESPONSE     98

/*
 * Local functions for orte_plm_yarn_module
 */
static int plm_yarn_init(void);
static int plm_yarn_launch_job(orte_job_t *jdata);
static int plm_yarn_terminate_orteds(void);
static int plm_yarn_signal_job(orte_jobid_t jobid, int32_t signal);
static int plm_yarn_finalize(void);

/*
 * Local utility functions
 */
static void process_state_monitor_cb(int fd, short args, void *cbdata);
static void heartbeat_with_AM_cb(int fd, short event, void *data);
static void finish_app_master(bool succeed, bool check_proc_status);

/*
 * Local functions for added job state's called in plm_yarn_init
 */
static int setup_daemon_proc_env_and_argv(orte_proc_t* proc, char ***pargv,
        int *argc, char ***penv);
static int common_launch_process(orte_job_t *jdata, bool launch_daemon, int *launched_proc_num);
static void launch_daemons(int fd, short args, void* cbdata);


static int setup_proc_env_and_argv(orte_job_t* jdata, orte_app_context_t* app,
        orte_proc_t* proc, char ***pargv, char ***penv);
static int plm_yarn_actual_launch_procs(orte_job_t* jdata);
static void yarn_hnp_sync_recv(int status, orte_process_name_t* sender,
                           opal_buffer_t* buffer, orte_rml_tag_t tag,
                           void* cbdata);
static void plm_yarn_launch_apps(int fd, short args, void *cbdata);


static void plm_yarn_quit(int fd, short args, void* cbdata);
static void plm_yarn_cleanup_job(int fd, short args, void *cbdata);

static struct timeval daemon_launch_start;

/*
 * Global variable
 */
orte_plm_base_module_1_0_0_t orte_plm_yarn_module = {
	plm_yarn_init,
    orte_plm_base_set_hnp_name,
    plm_yarn_launch_job,
    NULL,
    orte_plm_base_orted_terminate_job,
    plm_yarn_terminate_orteds,
    orte_plm_base_orted_kill_local_procs,
    plm_yarn_signal_job,
    plm_yarn_finalize
};

/*
 * Local variables
 */
extern char** environ;
static int num_sync_daemons = 0;
static bool appmaster_finished = false;


/**
* Init the module
 */
static int plm_yarn_init(void)
{
    int rc;

    if (ORTE_SUCCESS != (rc = orte_plm_base_comm_start())) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }

        /* point to our launch apps callback */
	if (ORTE_SUCCESS
			!= (rc = orte_state.add_job_state(ORTE_JOB_STATE_LAUNCH_DAEMONS,
					launch_daemons, ORTE_SYS_PRI))) {
		ORTE_ERROR_LOG(rc);
		return rc;
	}

    /* point to our launch apps callback */
	if (ORTE_SUCCESS
			!= (rc = orte_state.add_job_state(ORTE_JOB_STATE_LAUNCH_APPS,
					plm_yarn_launch_apps, ORTE_SYS_PRI))) {
		ORTE_ERROR_LOG(rc);
		return rc;
	}

	/* set quit callback */
	if (ORTE_SUCCESS
			!= (rc = orte_state.set_job_state_callback(
					ORTE_JOB_STATE_ALL_JOBS_COMPLETE, plm_yarn_quit))) {
		ORTE_ERROR_LOG(rc);
		opal_output(0,
				"%s plm:yarn:plm_yarn_init: set job state callback with ORTE_JOB_STATE_ALL_JOBS_COMPLETE failed.",
				ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
		exit(rc);
	}

	if (ORTE_SUCCESS
			!= (rc = orte_state.set_job_state_callback(
					ORTE_JOB_STATE_DAEMONS_TERMINATED, plm_yarn_quit))) {
		ORTE_ERROR_LOG(rc);
		opal_output(0,
				"%s plm:yarn:plm_yarn_init: set job state callback with ORTE_JOB_STATE_DAEMONS_TERMINATED failed.",
				ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
		exit(rc);
	}

	if (ORTE_SUCCESS
			!= (rc = orte_state.set_job_state_callback(
					ORTE_JOB_STATE_NOTIFY_COMPLETED, plm_yarn_cleanup_job))) {
		ORTE_ERROR_LOG(rc);
		opal_output(0,
				"%s plm:yarn:init: set job state callback with ORTE_JOB_STATE_NOTIFY_COMPLETED failed",
				ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
		exit(rc);
	}

    return rc;
}


static void process_state_monitor_cb(int fd, short args, void *cbdata)
{
    orte_proc_t *proc = (orte_proc_t*)cbdata;

    // double check if we need change state
    if(proc->state >= ORTE_PROC_STATE_TERMINATED) {
        return;
    } else {
        // orted not send proc state to us, so we simply change it to NON_ZERO exit
    	proc->state = ORTE_PROC_STATE_TERM_NON_ZERO;
		ORTE_ACTIVATE_PROC_STATE(&proc->name, ORTE_PROC_STATE_TERM_NON_ZERO);
    }
}

static void heartbeat_with_AM_cb(int fd, short event, void *data)
{
    int i, rc;
    orte_job_t *jdata = (orte_job_t*)data;
    orte_job_t* daemons = orte_get_job_data_object(ORTE_PROC_MY_NAME->jobid);

    completed_proc_t *completed_proc_array = NULL;
    int array_size = 0;

    if (0 != heartbeat_with_am(&completed_proc_array, &array_size) || array_size < 0) {
    	opal_output(0, "%s plm:yarn:heartbeat_with_AM_cb: heatbeat_with_am failed, please check",
    			ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
    	goto cleanup;
    }

    for (i = 0; i < array_size; i++) {
    	uint32_t local_jobid = completed_proc_array[i].proc_name.jobid;
    	uint32_t vpid = completed_proc_array[i].proc_name.vpid;
    	uint32_t exit_value = completed_proc_array[i].exit_value;

    	 /* next, we will modify proc's state */
    	orte_job_t* tmp_jdata = (orte_job_t*) opal_pointer_array_get_item(orte_job_data, local_jobid);
    	orte_proc_t* proc = (orte_proc_t*) opal_pointer_array_get_item(tmp_jdata->procs, vpid);

    	/* if this process is already terminated, just skip over */
		if (proc->state >= ORTE_PROC_STATE_TERMINATED) {
			continue;
		}

		if (exit_value == 0) {
			/* simply active proc state, that's no problem */
			ORTE_ACTIVATE_PROC_STATE(&proc->name, ORTE_PROC_STATE_TERMINATED);
		} else if (exit_value == -1000 || exit_value == -100 || exit_value == -101) {
			opal_output(0, "%s plm:yarn:heartbeat_with_AM_cb proc failed to start", ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
			ORTE_ERROR_LOG(ORTE_ERROR);
			proc->state = ORTE_PROC_STATE_FAILED_TO_START;
			ORTE_ACTIVATE_PROC_STATE(&proc->name, ORTE_PROC_STATE_FAILED_TO_LAUNCH);
		} else {
			/* here, means currently the proc's state < ORTE_PROC_STATE_TERMINATED,
			 * however, from AM's heartbeat response, we got the proc's container is terminated,
			 * to solve this dilemma , we set a timer event to reconfirm this proc's state,
			 */
			opal_event_t *ev = NULL;
			ev = (opal_event_t*) malloc(sizeof(opal_event_t));

			struct timeval delay;
			delay.tv_sec = 15;
			delay.tv_usec = 0;

			opal_event_evtimer_set(orte_event_base, ev, process_state_monitor_cb, proc);
			opal_event_evtimer_add(ev, &delay);
		}
    }

cleanup:
	free_completed_proc_array(completed_proc_array);

    /* next heartbeat */
    opal_event_t *ev = NULL;
    ev = (opal_event_t*) malloc(sizeof(opal_event_t));

    struct timeval delay;
    delay.tv_sec = 1;
    delay.tv_usec = 0;

    opal_event_evtimer_set(orte_event_base, ev, heartbeat_with_AM_cb, jdata);
	opal_event_evtimer_add(ev, &delay);
}

/* When working in this function, ALWAYS jump to "cleanup" if
 * you encounter an error so that orterun will be woken up and
 * the job can cleanly terminate
 */
static int plm_yarn_launch_job(orte_job_t *jdata)
{
    if (ORTE_JOB_CONTROL_RESTART & jdata->controls) {
        /* this is a restart situation - skip to the mapping stage */
        ORTE_ACTIVATE_JOB_STATE(jdata, ORTE_JOB_STATE_MAP);
    } else {
        /* new job - set it up */
        ORTE_ACTIVATE_JOB_STATE(jdata, ORTE_JOB_STATE_INIT);
    }
    return ORTE_SUCCESS;
}

/*
 * setup env and argv for specified process
 */
static int setup_proc_env_and_argv(orte_job_t* jdata, orte_app_context_t* app,
        orte_proc_t* proc, char ***pargv, char ***penv)
{
    char* param;
    char* param2;
    char* value;
    char* vp_id_str;
    char* job_id_str;
    int rc;
    int i, num_nodes;

    /* obtain app->argv */
    if (!(app->argv)) {
        opal_output(0, "%s plm::yarn::setup_proc_env_and_argv: app->argv is null",
            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
        return ORTE_ERROR;
    }
    *pargv = opal_argv_copy(app->argv);

    if (ORTE_SUCCESS != orte_util_convert_jobid_to_string(&job_id_str, jdata->jobid)) {
        ORTE_ERROR_LOG(ORTE_ERR_OUT_OF_RESOURCE);
        return ORTE_ERR_OUT_OF_RESOURCE;
    }
    if (ORTE_SUCCESS != orte_util_convert_vpid_to_string(&vp_id_str, proc->name.vpid)) {
        ORTE_ERROR_LOG(ORTE_ERR_OUT_OF_RESOURCE);
        return ORTE_ERR_OUT_OF_RESOURCE;
    }

    // add stdout, stderr to app
    opal_argv_append_nosize(pargv, "1><LOG_DIR>/stdout");
    opal_argv_append_nosize(pargv, "2><LOG_DIR>/stderr");

    // add java executor to app
    opal_argv_prepend_nosize(pargv, vp_id_str);
    opal_argv_prepend_nosize(pargv, job_id_str);
    opal_argv_prepend_nosize(pargv, "com.pivotal.hamster.yarnexecutor.YarnExecutor");
    opal_argv_prepend_nosize(pargv, "hamster-core.jar");
    opal_argv_prepend_nosize(pargv, "-cp");
    opal_argv_prepend_nosize(pargv, getenv("HAMSTER_JAVA_OPT")==NULL ? "-Xmx32M -Xms8M" : getenv("HAMSTER_JAVA_OPT"));
    opal_argv_prepend_nosize(pargv, "$JAVA_HOME/bin/java");

    /* obtain app->env */
    *penv = opal_environ_merge(environ, app->env);

    if (!proc->node) {
        opal_output(0, "%s plm::yarn::setup_proc_env_and_argv: node of proc[%d] is NULL",
            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), proc->name.vpid);
        return ORTE_ERROR;
    }

    if (!proc->node->daemon) {
        opal_output(0, "%s plm::yarn::setup_proc_env_and_argv: daemon of node[%s] is NULL",
            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), proc->node->name);
        return ORTE_ERROR;
    }

    /* set the app_context number into the environment */ //??
//    param = mca_base_param_env_var("orte_app_num");
    mca_base_var_env_name("orte_app_num", &param);
    asprintf(&param2, "%ld", (long)app->idx);
    opal_setenv(param, param2, true, penv);
    free(param);
    free(param2);

    // pass the daemon's name
//    param = mca_base_param_env_var("orte_local_daemon_uri");
    mca_base_var_env_name("orte_local_daemon_uri", &param);
    opal_setenv(param, proc->node->daemon->rml_uri, true, penv);
    free(param);

    /* pass my contact info */
//    param = mca_base_param_env_var("orte_hnp_uri");
    mca_base_var_env_name("orte_hnp_uri", &param);
    opal_setenv(param, orte_process_info.my_hnp_uri, true, penv);
    free(param);

    /* pass the jobid */
//    param = mca_base_param_env_var("orte_ess_jobid");
    mca_base_var_env_name("orte_ess_jobid", &param);
    opal_setenv(param, job_id_str, true, penv);
    free(param);
    free(job_id_str);

    /* pass the rank */
    //param = mca_base_param_env_var("orte_ess_vpid");
    mca_base_var_env_name("orte_ess_vpid", &param);
    opal_setenv(param, vp_id_str, true, penv);
    free(param);


    opal_setenv("OMPI_COMM_WORLD_RANK", vp_id_str, true, penv);
    free(vp_id_str);  /* done with this now */

    /* pass local rank */
    asprintf(&value, "%lu", (unsigned long) proc->local_rank);
    opal_setenv("OMPI_COMM_WORLD_LOCAL_RANK", value, true, penv);
    free(value);

    /* pass node rank */
    asprintf(&value, "%lu", (unsigned long) proc->node_rank);
    opal_setenv("OMPI_COMM_WORLD_NODE_RANK", value, true, penv);

    /* set an mca param for it too */
    //param = mca_base_param_env_var("orte_ess_node_rank");
    mca_base_var_env_name("orte_ess_node_rank", &param);
    opal_setenv(param, value, true, penv);
    free(param);
    free(value);

    /* pass a param telling the child what model of cpu we are on,
     * if we know it
     */
    if (NULL != orte_local_cpu_type) {
//        param = mca_base_param_env_var("orte_cpu_type");
    	mca_base_var_env_name("orte_cpu_type", &param);
        /* do not overwrite what the user may have provided */
        opal_setenv(param, orte_local_cpu_type, false, penv);
        free(param);
    }
    if (NULL != orte_local_cpu_model) {
//        param = mca_base_param_env_var("orte_cpu_model");
    	mca_base_var_env_name("orte_cpu_model", &param);
        /* do not overwrite what the user may have provided */
        opal_setenv(param, orte_local_cpu_model, false, penv);
        free(param);
    }

    /* pass the number of nodes involved in this job */
//    param = mca_base_param_env_var("orte_num_nodes");
    mca_base_var_env_name("orte_num_nodes", &param);

    /* we have to count the number of nodes as the size of orte_node_pool
     * is only guaranteed to be equal or larger than that number - i.e.,
     * the pointer_array increases the size by a block each time, so some
     * of the locations are left empty
     */
    num_nodes = 0;
    for (i = 0; i < orte_node_pool->size; i++) {
        if (NULL != opal_pointer_array_get_item(orte_node_pool, i)) {
            num_nodes++;
        }
    }
    asprintf(&value, "%d", num_nodes);
    opal_setenv(param, value, true, penv);
    free(param);
    free(value);

    /* setup yield schedule */
//    param = mca_base_param_env_var("mpi_yield_when_idle");
    mca_base_var_env_name("mpi_yield_when_idle", &param);
    opal_setenv(param, "0", false, penv);
    free(param);

    /* set MPI universe envar */
    orte_ess_env_put(jdata->num_procs, proc->node->num_procs, penv);

    asprintf(&value, "%ld", (long) jdata->num_procs);
    opal_setenv("OMPI_UNIVERSE_SIZE", value, true, penv);
    free(value);

	/* pass collective ids for the std MPI operations */
//	param = mca_base_param_env_var("orte_peer_modex_id");
    mca_base_var_env_name("orte_peer_modex_id", &param);
	asprintf(&value, "%d", jdata->peer_modex);
	opal_setenv(param, value, true, penv);
	free(param);
	free(value);

//	param = mca_base_param_env_var("orte_peer_init_barrier_id");
	mca_base_var_env_name("orte_peer_init_barrier_id", &param);
	asprintf(&value, "%d", jdata->peer_init_barrier);
	opal_setenv(param, value, true, penv);
	free(param);
	free(value);

//	param = mca_base_param_env_var("orte_peer_fini_barrier_id");
	mca_base_var_env_name("orte_peer_fini_barrier_id", &param);
	asprintf(&value, "%d", jdata->peer_fini_barrier);
	opal_setenv(param, value, true, penv);
	free(param);
	free(value);

    /* finally, we will set/unset some mca param to select modules */
    opal_unsetenv("OMPI_MCA_plm", penv);
    opal_unsetenv("OMPI_MCA_ras", penv);
    opal_unsetenv("OMPI_MCA_ess", penv);
    opal_unsetenv("OMPI_MCA_state", penv);
    opal_unsetenv("OMPI_MCA_errmgr", penv);
    return 0;
}

/*
typedef struct {
	char **en_vars_array;
	char *args;
	char *host_name;
	process_name_t proc_name;
} launch_context_t;

typedef struct {
	bool is_successful;
	process_name_t proc_name;
} launch_response_t;


extern void build_launch_request(launch_context_t *launch_context_array, int array_size, avro_slice_t **slice);

extern void parse_launch_response(avro_slice_t *slice, launch_response_t **launch_response_array, int *array_size);

extern void free_launch_context_array(launch_context_t *launch_context_array, int array_size);

extern void free_launch_response_array(launch_response_t *launch_response_array);

 */
static int common_launch_process(orte_job_t *jdata, bool launch_daemon, int *launched_proc_num)
{
	int i, rc;
	orte_proc_t* proc = NULL;
	char **argv;
	int argc;
	char **env;
	bool error_flag = false;
	int launched_num = 0;
    int array_size;

    array_size = launch_daemon ? jdata->num_procs-1 : jdata->num_procs;
	
    /* we first malloc the launch_context_array */
	launch_context_t *launch_context_array = NULL;
    launch_context_array = xmalloc(sizeof(launch_context_t) * array_size);

	/* when launch_daemon, start from 1 because we don't need launch HNP process */
	i = launch_daemon ? 1 : 0;

	for (; i < jdata->num_procs; i++) {
		argv = NULL;
		argc = 0;
		env = NULL;
		/* setup env/argv  */
		proc = opal_pointer_array_get_item(jdata->procs, i);
		if (!proc) {
			opal_output(0, "%s plm:yarn:common_launch_process: proc[%d] is NULL",
					ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), i);
			ORTE_ERROR_LOG(ORTE_ERROR_DEFAULT_EXIT_CODE);
		}

		if (launch_daemon) {
			rc = setup_daemon_proc_env_and_argv(proc, &argv, &argc, &env);
		} else {
			orte_app_context_t* app = (orte_app_context_t*) opal_pointer_array_get_item(jdata->apps, proc->app_idx);
			rc = setup_proc_env_and_argv(jdata, app, proc, &argv, &env);
		}
		if (0 != rc) {
			opal_output(0,
					"%s plm:yarn:common_launch_process: failed to setup env/argv of proc[%d]",
					ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), i);
			ORTE_ERROR_LOG(ORTE_ERROR_DEFAULT_EXIT_CODE);
			error_flag = true;
			goto cleanup;
		}

		 /* print launch commandline and env when this env is specified */
		if (getenv("HAMSTER_VERBOSE")) {

			char* join_argv = opal_argv_join(argv, ' ');

			OPAL_OUTPUT_VERBOSE((5, orte_plm_base_framework.framework_output, "%s plm:yarn:common_launch_process: launch argv=%s",
							ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), join_argv));
			if (join_argv) {
				free(join_argv);
			}
		}

		OPAL_OUTPUT_VERBOSE((5, orte_plm_base_framework.framework_output,
									"%s plm:yarn:common_launch_process: after setup env and argv for proc=%d.",
									ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), i));
		/*
		typedef struct {
			char **en_vars_array;
			char *args;
			char *host_name;
			process_name_t proc_name;
		} launch_context_t;
		*/

		/* 1. now start packing launch_context_array */
	    int j;
        j = launch_daemon ? i-1 : i;
        char **tmp_env = env;
		launch_context_array[j].en_vars_array = opal_argv_copy(tmp_env);
		char* join_argv = opal_argv_join(argv, ' ');
		launch_context_array[j].args = strdup(join_argv);
		launch_context_array[j].host_name = strdup(proc->node->name);
		launch_context_array[j].proc_name.jobid = ORTE_LOCAL_JOBID(proc->name.jobid);
		launch_context_array[j].proc_name.vpid = proc->name.vpid;

cleanup:
		/* free argv and env for this proc */
		if (argv) {
			opal_argv_free(argv);
		}
		if (env) {
			opal_argv_free(env);
		}
		if (join_argv) {
			free(join_argv);
		}
		if (error_flag) {
			ORTE_UPDATE_EXIT_STATUS(ORTE_ERROR_DEFAULT_EXIT_CODE);
			free_launch_context_array(launch_context_array, array_size);
			return ORTE_ERROR;
		}
	}

	/* 2. send launch procs request and recv response msg */
	launch_response_t *launch_response_array = NULL;
	int response_array_size = 0;
    rc = launch_procs_with_am(launch_context_array, array_size, &launch_response_array, &response_array_size);
	if (0 != rc) {
		opal_output(0, "%s plm:yarn:common_process_launch: error happened when launch procs with AM",
				ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
		goto launch_failed;
	}

	/* 3. parse the response */
	/*
	typedef struct {
		bool is_successful;
		process_name_t proc_name;
	} launch_response_t;
	*/
	for (i = 0; i < response_array_size; i++) {
		orte_jobid_t local_jobid = launch_response_array[i].proc_name.jobid;
		orte_vpid_t vpid = launch_response_array[i].proc_name.vpid;
		bool success = launch_response_array[i].is_successful;
		orte_proc_t* proc = (orte_proc_t*) opal_pointer_array_get_item(jdata->procs, vpid);
		if (success) {
			ORTE_ACTIVATE_PROC_STATE(&proc->name, ORTE_PROC_STATE_RUNNING);
			launched_num++;
		} else {
			opal_output(0,
					"%s plm:yarn:common_process_launch: launch proc failed when jobid = %u, vpid = %u",
					ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), local_jobid, vpid);
			proc->state = ORTE_PROC_STATE_FAILED_TO_START;
			ORTE_ACTIVATE_PROC_STATE(&proc->name, ORTE_PROC_STATE_FAILED_TO_START);
			goto launch_failed;
		}
	}

	/* to return back */
	*launched_proc_num = launched_num;
	free_launch_context_array(launch_context_array, array_size);
	free_launch_response_array(launch_response_array);
	return ORTE_SUCCESS;

launch_failed:
	free_launch_context_array(launch_context_array, array_size);
	free_launch_response_array(launch_response_array);
	ORTE_UPDATE_EXIT_STATUS(ORTE_ERROR_DEFAULT_EXIT_CODE);
	return ORTE_ERROR;
}

/* setup argv for daemon process */
static int setup_daemon_proc_env_and_argv(orte_proc_t* proc, char ***pargv,
        int *argc, char ***penv)
{
    orte_job_t* daemons;
    int rc;
    char* param;

    /* get daemon job object */
    daemons = orte_get_job_data_object(ORTE_PROC_MY_NAME->jobid);

    *penv = opal_argv_copy(orte_launch_environ);

    /* prepend orted to argv */
//    opal_argv_append(argc, pargv, "orted");
    opal_argv_append(argc, pargv, "hrted");

    /* ess */
    opal_argv_append(argc, pargv, "-mca");
    opal_argv_append(argc, pargv, "ess");
    opal_argv_append(argc, pargv, "env");

    /* jobid */
    opal_argv_append(argc, pargv, "-mca");
    opal_argv_append(argc, pargv, "orte_ess_jobid");
    if (ORTE_SUCCESS != (rc = orte_util_convert_jobid_to_string(&param, ORTE_PROC_MY_NAME->jobid))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    opal_argv_append(argc, pargv, param);
    free(param);

    /* vpid */
    opal_argv_append(argc, pargv, "-mca");
    opal_argv_append(argc, pargv, "orte_ess_vpid");
    if (ORTE_SUCCESS != (rc = orte_util_convert_vpid_to_string(&param, proc->name.vpid))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    opal_argv_append(argc, pargv, param);
    free(param);

    /* num processes */
    opal_argv_append(argc, pargv, "-mca");
    opal_argv_append(argc, pargv, "orte_ess_num_procs");
    asprintf(&param, "%lu", daemons->num_procs);
    opal_argv_append(argc, pargv, param);
    free(param);

	/* pass the uri of the hnp */
	asprintf(&param, "\\\"%s\\\"", orte_rml.get_contact_info());
	opal_argv_append(argc, pargv, "-mca");
	opal_argv_append(argc, pargv, "orte_hnp_uri");
	opal_argv_append(argc, pargv, param);
	free(param);

    /* oob */
    opal_argv_append(argc, pargv, "-mca");
    opal_argv_append(argc, pargv, "oob");
    opal_argv_append(argc, pargv, "tcp");

    /* odls */
    opal_argv_append(argc, pargv, "-mca");
    opal_argv_append(argc, pargv, "odls");
    opal_argv_append(argc, pargv, "yarn");

    /* add stdout, stderr to orted */
    opal_argv_append_nosize(pargv, "1><LOG_DIR>/stdout");
    opal_argv_append_nosize(pargv, "2><LOG_DIR>/stderr");


    /* orted */
	opal_argv_append(argc, pargv, "-mca");
	opal_argv_append(argc, pargv, "state");
	opal_argv_append(argc, pargv, "orted");

    /* print launch commandline and env when this env is specified */
    if (getenv("HAMSTER_VERBOSE")) {
        char* join_argv = opal_argv_join(*pargv, ' ');
        OPAL_OUTPUT_VERBOSE((5, orte_plm_base_framework.framework_output, "%s plm:yarn launch_daemon argv=%s",
            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), join_argv));
        if (join_argv) {
            free(join_argv);
        }
    }
    return 0;
}



static void launch_daemons(int fd, short args, void *cbdata)
{
    orte_state_caddy_t *state = (orte_state_caddy_t*)cbdata;
    orte_job_map_t *map;
    int rc;
    bool failed_launch = true;
    orte_job_t *daemons;
    int launched_proc_num = 0;

    OPAL_OUTPUT_VERBOSE((1, orte_plm_base_framework.framework_output,
                         "%s plm:yarn:launch_daemons: LAUNCH DAEMONS CALLED",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));

    /*
     * set the time anchor of daemon_launch_start, for log
     */
    gettimeofday(&daemon_launch_start, NULL);

    /* if we are launching debugger daemons, then just go
     * do it - no new daemons will be launched
     */
    if (ORTE_JOB_CONTROL_DEBUGGER_DAEMON & state->jdata->controls) {
        state->jdata->state = ORTE_JOB_STATE_DAEMONS_LAUNCHED;
        ORTE_ACTIVATE_JOB_STATE(state->jdata, ORTE_JOB_STATE_DAEMONS_REPORTED);
        OBJ_RELEASE(state);
        return;
    }

    /* start by setting up the virtual machine */
    daemons = orte_get_job_data_object(ORTE_PROC_MY_NAME->jobid);
    if (ORTE_SUCCESS != (rc = orte_plm_base_setup_virtual_machine(state->jdata))) {
        ORTE_ERROR_LOG(rc);
        goto cleanup;
    }

   /* if we don't want to launch, then don't attempt to
     * launch the daemons - the user really wants to just
     * look at the proposed process map
     */
    if (orte_do_not_launch) {
        /* set the state to indicate the daemons reported - this
         * will trigger the daemons_reported event and cause the
         * job to move to the following step
         */
        state->jdata->state = ORTE_JOB_STATE_DAEMONS_LAUNCHED;
        ORTE_ACTIVATE_JOB_STATE(state->jdata, ORTE_JOB_STATE_DAEMONS_REPORTED);
        OBJ_RELEASE(state);
        return;
    }

    /* Get the map for this job */
    if (NULL == (map = daemons->map)) {
        ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
        rc = ORTE_ERR_NOT_FOUND;
        goto cleanup;
    }

    if (0 == map->num_new_daemons) {
        /* set the state to indicate the daemons reported - this
         * will trigger the daemons_reported event and cause the
         * job to move to the following step
         */
        OPAL_OUTPUT_VERBOSE((1, orte_plm_base_framework.framework_output,
                             "%s plm:yarn:launch_daemons: no new daemons to launch",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
        state->jdata->state = ORTE_JOB_STATE_DAEMONS_LAUNCHED;
        if (ORTE_JOB_STATE_DAEMONS_REPORTED == daemons->state) {
            ORTE_ACTIVATE_JOB_STATE(state->jdata, ORTE_JOB_STATE_DAEMONS_REPORTED);
        }
        OBJ_RELEASE(state);
        return;
    }

	rc = common_launch_process(daemons, true, &launched_proc_num);
	if (rc != ORTE_SUCCESS) {
		ORTE_ERROR_LOG(rc);
		goto cleanup;
	}

	/* if all daemon procs are launched successfully, then modify the job's state */
	if (launched_proc_num == (daemons->num_procs - 1)) {
		/* indicate that the daemons for this job were launched */
		state->jdata->state = ORTE_JOB_STATE_DAEMONS_LAUNCHED;
		daemons->state = ORTE_JOB_STATE_DAEMONS_LAUNCHED;
        ORTE_ACTIVATE_JOB_STATE(state->jdata, ORTE_JOB_STATE_DAEMONS_LAUNCHED);
        ORTE_ACTIVATE_JOB_STATE(daemons, ORTE_JOB_STATE_DAEMONS_LAUNCHED);
		OPAL_OUTPUT_VERBOSE((5, orte_plm_base_framework.framework_output,
						"%s plm:yarn:launch_daemons: launch daemon proc successfully with AM",
						ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
	}

    /* flag that launch was successful, so far as we currently know */
    failed_launch = false;

cleanup:
    /* cleanup the caddy */
    OBJ_RELEASE(state);

    /* check for failed launch - if so, force terminate */
    if (failed_launch) {
    	ORTE_FORCED_TERMINATE(ORTE_ERROR_DEFAULT_EXIT_CODE);
    }
}

static int plm_yarn_actual_launch_procs(orte_job_t* jdata)
{
    int rc;

    int launched_proc_num = 0;

    OPAL_OUTPUT_VERBOSE((5, orte_plm_base_framework.framework_output,
                    "%s plm:yarn:plm_yarn_actual_launch_procs for job %s",
                    ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                    ORTE_JOBID_PRINT(jdata->jobid)));


    rc = common_launch_process(jdata, false, &launched_proc_num);

	if (rc != ORTE_SUCCESS) {
		return rc;
	}

	return ORTE_SUCCESS;
}

static void yarn_hnp_sync_recv(int status, orte_process_name_t* sender,
                           opal_buffer_t* buffer, orte_rml_tag_t tag,
                           void* cbdata)
{
    /* get daemon job object */
    orte_job_t* daemons = orte_get_job_data_object(ORTE_PROC_MY_NAME->jobid);
    /* get user's job object */
    orte_job_t* jdata = (orte_job_t*)cbdata;
    opal_buffer_t *msg;
    int rc;

    num_sync_daemons++;

    OPAL_OUTPUT_VERBOSE((5, orte_plm_base_framework.framework_output,
                "%s plm:yarn:yarn_hnp_sync_recv: we got [%d/%d] daemons yarn sync request",
                ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), num_sync_daemons, daemons->num_procs));

    /* we got all daemons synced */
    if (daemons->num_procs == num_sync_daemons) {
        OPAL_OUTPUT_VERBOSE((5, orte_plm_base_framework.framework_output,
                        "%s plm:yarn:yarn_hnp_sync_recv: we got all daemons sync, will launch proc in NM",
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));

        msg = OBJ_NEW(opal_buffer_t);
        if (ORTE_SUCCESS != (rc = orte_grpcomm.xcast(ORTE_PROC_MY_NAME->jobid, msg, ORTE_RML_TAG_YARN_SYNC_RESPONSE))) {
            ORTE_ERROR_LOG(rc);
            opal_output(0, "%s plm:yarn:yarn_hnp_sync_recv: failed to send sync response to daemon processes.",
                ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
            jdata->state = ORTE_JOB_STATE_FAILED_TO_LAUNCH;
            ORTE_ACTIVATE_JOB_STATE(jdata, ORTE_JOB_STATE_FAILED_TO_LAUNCH);
            OBJ_RELEASE(msg);
        }
        OBJ_RELEASE(msg);

        /*
         * compute the timecost_daemon_launch
         */
        struct timeval daemon_launch_end;
        gettimeofday(&daemon_launch_end, NULL);
        float timecost = ((daemon_launch_end.tv_sec - daemon_launch_start.tv_sec) * 1000000 +
        		(daemon_launch_end.tv_usec - daemon_launch_start.tv_usec)) / (float) 1000000;
        opal_output(0, "STATISTIC: hamster timecost_daemon_launch = %f", timecost);

        struct timeval proc_launch_start;
        gettimeofday(&proc_launch_start, NULL);
        opal_output(0, "STATISTIC: hamster proc_launch_start.tv_sec = %ld, proc_launch_start.tv_usec = %ld",
        		proc_launch_start.tv_sec, proc_launch_start.tv_usec);

        /* here, we actually launch procs */
        plm_yarn_actual_launch_procs(jdata);
    }
}

static void plm_yarn_launch_apps(int fd, short args, void *cbdata)
{
    int rc;
    orte_job_t *jdata;
    orte_state_caddy_t *caddy = (orte_state_caddy_t*)cbdata;

    /* convenience */
    jdata = caddy->jdata;

    if (ORTE_JOB_STATE_LAUNCH_APPS != caddy->job_state) {
    	ORTE_FORCED_TERMINATE(ORTE_ERROR_DEFAULT_EXIT_CODE);
        return;
    }

    /* update job state */
    jdata->state = caddy->job_state;

    /* register recv callback for daemons sync request */
    orte_rml.recv_buffer_nb(ORTE_NAME_WILDCARD, ORTE_RML_TAG_YARN_SYNC_REQUEST,
    		ORTE_RML_PERSISTENT, yarn_hnp_sync_recv, jdata);

    orte_plm_base_launch_apps(fd, args, cbdata);

	//============heartbeat with AM======
	opal_event_t *ev = NULL;
	ev = (opal_event_t*) malloc(sizeof(opal_event_t));

	struct timeval delay;
	delay.tv_sec = 1;
	delay.tv_usec = 0;

	opal_event_evtimer_set(orte_event_base, ev, heartbeat_with_AM_cb, jdata);
	opal_event_evtimer_add(ev, &delay);
	//===================================
}

static void finish_app_master(bool succeed, bool check_proc_status)
{
    int rc;
    int i, j;
    char *diag_msg = "finish_app_master";

    if (appmaster_finished) {
    	return;
    }

    /* it's a workaround to disable this checking for a weird error of
     * job terminated with exit_code = 0, but one of the proc's state
     * still unterminated, need more tests on this to verify if this
     * work around impact on other functionality
     */

    // we need double check if any proc failed
    if (succeed && check_proc_status) {
    	// start with 1 because we don't want to check daemon's proc
        for (i = 1; i < orte_job_data->size; i++) {
            orte_job_t* job = opal_pointer_array_get_item(orte_job_data, i);
            if (!job) {
                continue;
            }
            for (j = 0; j < job->procs->size; j++) {
                orte_proc_t* proc = opal_pointer_array_get_item(job->procs, j);
                if (!proc) {
                    continue;
                }
                // if any process is non-terminated, we will consider it's error
                if (proc->state < ORTE_PROC_STATE_TERMINATED && proc->state != ORTE_PROC_STATE_WAITPID_FIRED) {
                    succeed = false;
                    break;
                }
            }
            if (!succeed) {
                break;
            }
        }
    }


    /* send finish request and recv response */
    rc = finish_with_am(succeed, diag_msg);
    if (0 != rc) {
    	opal_output(0,
				"%s plm:yarn:finish_app_master: error happened when finish with AM",
				ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
    	exit(-1);
    }

    appmaster_finished = true;
}

/**
* Terminate the orteds for a given job
 */
static int plm_yarn_terminate_orteds(void)
{
	finish_app_master(0 == orte_exit_status, true);
    orte_job_t* jdata = orte_get_job_data_object(ORTE_PROC_MY_NAME->jobid);
    if (ORTE_JOB_STATE_DAEMONS_TERMINATED != jdata->state) {
		/* need to set the #terminated value to avoid an incorrect error msg */
		jdata->num_terminated = jdata->num_procs;
        ORTE_ACTIVATE_JOB_STATE(jdata, ORTE_JOB_STATE_DAEMONS_TERMINATED);
    }
    return ORTE_SUCCESS;
}

/**
 * Signal all the processes in the child srun by sending the signal directly to it
 */
static int plm_yarn_signal_job(orte_jobid_t jobid, int32_t signal)
{
	return ORTE_SUCCESS;
}


static int plm_yarn_finalize(void)
{
    int rc;

    /* cleanup any pending recvs */
    if (ORTE_SUCCESS != (rc = orte_plm_base_comm_stop())) {
        ORTE_ERROR_LOG(rc);
    }

    return ORTE_SUCCESS;
}


static void plm_yarn_quit(int fd, short args, void* cbdata)
{
    finish_app_master(0 == orte_exit_status, true);
    orte_quit(fd, args, cbdata);
}

static void plm_yarn_cleanup_job(int fd, short argc, void *cbdata)
{
    orte_state_caddy_t *caddy = (orte_state_caddy_t*) cbdata;
    orte_job_t *jdata = caddy->jdata;

    OPAL_OUTPUT_VERBOSE((5, orte_plm_base_framework.framework_output,
                    "%s plm:yarn:cleanup_job: on job %s",
                    ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                    (NULL == jdata) ? "NULL" : ORTE_JOBID_PRINT(jdata->jobid)));

    finish_app_master(0 == orte_exit_status, false);

    /* flag that we were notified */
    jdata->state = ORTE_JOB_STATE_NOTIFIED;
    /* send us back thru job complete */
    ORTE_ACTIVATE_JOB_STATE(jdata, ORTE_JOB_STATE_TERMINATED);
    OBJ_RELEASE(caddy);
}
