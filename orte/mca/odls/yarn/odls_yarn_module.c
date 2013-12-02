/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2008 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2007-2010 Oracle and/or its affiliates.  All rights reserved.
 * Copyright (c) 2007      Evergrid, Inc. All rights reserved.
 * Copyright (c) 2008-2012 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2010      IBM Corporation.  All rights reserved.
 * Copyright (c) 2011-2012 Los Alamos National Security, LLC.  All rights
 *                         reserved. 
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

/*
 * There is a complicated sequence of events that occurs when the
 * parent forks a child process that is intended to launch the target
 * executable.
 *
 * Before the child process exec's the target executable, it might tri
 * to set the affinity of that new child process according to a
 * complex series of rules.  This binding may fail in a myriad of
 * different ways.  A lot of this code deals with reporting that error
 * occurately to the end user.  This is a complex task in itself
 * because the child process is not "really" an ORTE process -- all
 * error reporting must be proxied up to the parent who can use normal
 * ORTE error reporting mechanisms.
 *
 * Here's a high-level description of what is occurring in this file:
 *
 * - parent opens a pipe
 * - parent forks a child
 * - parent blocks reading on the pipe: the pipe will either close
 *   (indicating that the child successfully exec'ed) or the child will
 *   write some proxied error data up the pipe
 *
 * - the child tries to set affinity and do other housekeeping in
 *   preparation of exec'ing the target executable
 * - if the child fails anywhere along the way, it sends a message up
 *   the pipe to the parent indicating what happened -- including a 
 *   rendered error message detailing the problem (i.e., human-readable).
 * - it is important that the child renders the error message: there
 *   are so many errors that are possible that the child is really the
 *   only entity that has enough information to make an accuate error string
 *   to report back to the user.
 * - the parent reads this message + rendered string in and uses ORTE
 *   reporting mechanisms to display it to the user
 * - if the problem was only a warning, the child continues processing
 *   (potentially eventually exec'ing the target executable).
 * - if the problem was an error, the child exits and the parent
 *   handles the death of the child as appropriate (i.e., this ODLS
 *   simply reports the error -- other things decide what to do).
 */

#include "orte_config.h"
#include "orte/constants.h"
#include "orte/types.h"

#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#include <signal.h>
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif  /* HAVE_SYS_STAT_H */
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#include <sys/types.h>
#include <dirent.h>

#include "opal/mca/hwloc/hwloc.h"
#include "opal/mca/hwloc/base/base.h"
#include "opal/class/opal_pointer_array.h"
#include "opal/util/opal_environ.h"
#include "opal/util/show_help.h"
#include "opal/util/fd.h"

#include "orte/util/show_help.h"
#include "orte/runtime/orte_wait.h"
#include "orte/runtime/orte_globals.h"
#include "orte/mca/errmgr/errmgr.h"
#include "orte/mca/ess/ess.h"
#include "orte/mca/iof/base/iof_base_setup.h"
#include "orte/mca/plm/plm.h"
#include "orte/util/name_fns.h"

#include "orte/mca/odls/base/base.h"
#include "orte/mca/odls/base/odls_private.h"
#include "odls_yarn.h"


#include "orte/mca/state/state.h"
#include "orte/mca/sensor/sensor.h"
#include "orte/mca/rml/rml.h"

#include "opal/mca/event/event.h"

#define ORTE_RML_TAG_YARN_SYNC_REQUEST      97
#define ORTE_RML_TAG_YARN_SYNC_RESPONSE     98

static orte_jobid_t jobid;
 static bool sensor_started = false;

typedef struct {
	opal_object_t super;
	opal_event_t ev;
	orte_jobid_t jobid;
} yarn_local_procs_monitor_t;

OBJ_CLASS_INSTANCE(yarn_local_procs_monitor_t, opal_object_t, NULL, NULL);

/*
 * Module functions (function pointers used in a struct)
 */
static int orte_odls_yarn_launch_local_procs(opal_buffer_t *data);
static int orte_odls_yarn_kill_local_procs(opal_pointer_array_t *procs);
static int orte_odls_yarn_signal_local_procs(const orte_process_name_t *proc, int32_t signal);
static int orte_odls_yarn_restart_proc(orte_proc_t *child);

//static int orte_odls_yarn_deliver_message(orte_jobid_t job, opal_buffer_t *buffer, orte_rml_tag_t tag);

/*
 * Local Functions
 */
static void local_process_state_monitor_cb(int fd, short args, void *cbdata);
static orte_vpid_t get_vpid_from_normal_file(const char* filename);
static orte_vpid_t get_vpid_from_err_file(const char* filename);
static void yarn_daemon_sync_recv(int status, orte_process_name_t* sender,
                           opal_buffer_t* buffer, orte_rml_tag_t tag,
                           void* cbdata);
static void process_proc_exit_status(int status, orte_proc_t* proc);


/*
 * Module
 */
orte_odls_base_module_t orte_odls_yarn_module = {
    orte_odls_base_default_get_add_procs_data,
    orte_odls_yarn_launch_local_procs,
    orte_odls_yarn_kill_local_procs,
    orte_odls_yarn_signal_local_procs,
    orte_odls_base_default_deliver_message,
    orte_odls_base_default_require_sync,
    orte_odls_yarn_restart_proc
};


static int orte_odls_yarn_kill_local_procs(opal_pointer_array_t *procs)
{
    return ORTE_SUCCESS;
}

static int orte_odls_yarn_signal_local_procs(const orte_process_name_t *proc, int32_t signal)
{
    return ORTE_SUCCESS;
}


static int orte_odls_yarn_restart_proc(orte_proc_t *child)
{
	return ORTE_SUCCESS;
}

static void process_proc_exit_status(int status, orte_proc_t* proc) {
	orte_job_t *jobdat;
	orte_proc_t *cptr;
	int i;
	orte_proc_state_t state=ORTE_PROC_STATE_WAITPID_FIRED;

	/* always set iof_complete to true when process exited, because we don't have IOF at all!*/
	proc->iof_complete = true;

    /* if the child was previously flagged as dead, then just
     * ensure that its exit state gets reported to avoid hanging
     */
    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls:yarn:process_proc_exit_status: child process %s was already dead",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(&proc->name)));

    /* get the jobdat for this child */
    if (NULL == (jobdat = orte_get_job_data_object(proc->name.jobid))) {
        ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
        goto MOVEON;
    }

    /* if this is a debugger daemon, then just report the state
     * and return as we aren't monitoring it
     */
    if (ORTE_JOB_CONTROL_DEBUGGER_DAEMON & jobdat->controls)  {
        goto MOVEON;
    }

    /* if this child was ordered to die, then just pass that along
     * so we don't hang
     */
    if (ORTE_PROC_STATE_KILLED_BY_CMD == proc->state) {
        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls:yarn:process_proc_exit_status: child %s was ordered to die",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             ORTE_NAME_PRINT(&proc->name)));
        goto MOVEON;
    }

    /* determine the state of this process */
    if(WIFEXITED(status)) {
        /* set the exit status appropriately */
        proc->exit_code = WEXITSTATUS(status);

        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls:yarn:process_proc_exit_status: child %s exit code %d",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             ORTE_NAME_PRINT(&proc->name), proc->exit_code));

        if (proc->aborted) {
            /* even though the process exited "normally", it happened
             * via an orte_abort call, so we need to indicate this was
             * an "abnormal" termination.
             */
            OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                 "%s odls:yarn:process_proc_exit_status: child %s died by call to abort",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 ORTE_NAME_PRINT(&proc->name)));
            state = ORTE_PROC_STATE_CALLED_ABORT;
            goto MOVEON;
        }

        /* check to see if a sync was required and if it was received */
        if (proc->registered) {
            if (proc->deregistered) {
                /* if we did recv a finalize sync, or one is not required,
                 * then declare it normally terminated
                 * unless it returned with a non-zero status indicating the code
                 * felt it was non-normal
                 */
                if (0 != proc->exit_code) {
                    state = ORTE_PROC_STATE_TERM_NON_ZERO;
                    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                         "%s odls:yarn:process_proc_exit_status: child process %s terminated normally "
                                         "but with a non-zero exit status - it "
                                         "will be treated as an abnormal termination",
                                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                         ORTE_NAME_PRINT(&proc->name)));
                } else {
                    /* indicate the waitpid fired */
                    state = ORTE_PROC_STATE_WAITPID_FIRED;
                }
            } else {
                /* we required a finalizing sync and didn't get it, so this
                 * is considered an abnormal termination and treated accordingly
                 */
                state = ORTE_PROC_STATE_TERM_WO_SYNC;
                OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                     "%s odls:yarn:process_proc_exit_status: child process %s terminated normally "
                                     "but did not provide a required finalize sync - it "
                                     "will be treated as an abnormal termination",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                     ORTE_NAME_PRINT(&proc->name)));
            }
        } else {
            /* has any child in this job already registered? */
            for (i=0; i < orte_local_children->size; i++) {
                if (NULL == (cptr = (orte_proc_t*)opal_pointer_array_get_item(orte_local_children, i))) {
                    continue;
                }
                if (cptr->name.jobid != proc->name.jobid) {
                    continue;
                }
                if (cptr->registered) {
                    /* someone has registered, and we didn't before
                     * terminating - this is an abnormal termination unless
                     * the allowed_exit_without_sync flag is set
                     */
                    if (0 != proc->exit_code) {
                        state = ORTE_PROC_STATE_TERM_NON_ZERO;
                        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                             "%s odls:yarn:process_proc_exit_status: child process %s terminated normally "
                                             "but with a non-zero exit status - it "
                                             "will be treated as an abnormal termination",
                                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                             ORTE_NAME_PRINT(&proc->name)));
                    } else {
                        state = ORTE_PROC_STATE_TERM_WO_SYNC;
                        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                             "%s odls:yarn:process_proc_exit_status: child process %s terminated normally "
                                             "but did not provide a required init sync - it "
                                             "will be treated as an abnormal termination",
                                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                             ORTE_NAME_PRINT(&proc->name)));
                    }
                    goto MOVEON;
                }
            }
            /* if no child has registered, then it is possible that
             * none of them will. This is considered acceptable. Still
             * flag it as abnormal if the exit code was non-zero
             */
            if (0 != proc->exit_code) {
                state = ORTE_PROC_STATE_TERM_NON_ZERO;
            } else {
                state = ORTE_PROC_STATE_WAITPID_FIRED;
            }
        }

        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls:yarn:process_proc_exit_status: child process %s terminated %s",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             ORTE_NAME_PRINT(&proc->name),
                             (0 == proc->exit_code) ? "normally" : "with non-zero status"));
    } else {
        /* the process was terminated with a signal! That's definitely
         * abnormal, so indicate that condition
         */
        state = ORTE_PROC_STATE_ABORTED_BY_SIG;
        /* If a process was killed by a signal, then make the
         * exit code of orterun be "signo + 128" so that "prog"
         * and "orterun prog" will both yield the same exit code.
         *
         * This is actually what the shell does for you when
         * a process dies by signal, so this makes orterun treat
         * the termination code to exit status translation the
         * same way
         */
        proc->exit_code = WTERMSIG(status) + 128;

        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls:yarn:process_proc_exit_status: child process %s terminated with signal",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             ORTE_NAME_PRINT(&proc->name) ));
        /* Do not decrement the number of local procs here. That is handled in the errmgr */
    }

 MOVEON:
    ORTE_ACTIVATE_PROC_STATE(&proc->name, state);
}

static orte_vpid_t get_vpid_from_err_file(const char* filename)
{
    orte_vpid_t id = 0;
    char* ptr = filename;
    while (*ptr != '_') {
        id = id * 10 + ((*ptr) - 48);
        ptr++;
    }
    return id;
}

static orte_vpid_t get_vpid_from_normal_file(const char* filename)
{
    orte_vpid_t id = atoi(filename);
    return id;
}

static void local_process_state_monitor_cb(int fd, short args, void *cbdata) {
    yarn_local_procs_monitor_t *caddy;
    struct timeval tv = {1, 0};
    caddy = (yarn_local_procs_monitor_t*)cbdata;
    orte_jobid_t jobid = caddy->jobid;
    int rc;

    orte_job_t* job = orte_get_job_data_object(jobid);
    if (!job) {
        ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
        goto error_out;
    }

    // do monitoring logic
    // get processes pid path
    char pid_dir[1024];
    sprintf(pid_dir, "../%u", jobid);

    // see files in pid path
    DIR* dirp = opendir(pid_dir);
    char full_file_name[4096];
    struct dirent *ent;
    int launched_proc_num = 0;
    int finished_proc_num = 0;

    if (dirp) {
        while ((ent = readdir(dirp)) != NULL) {
            if ((ent->d_name) && (strlen(ent->d_name) > 0)) {
                // if ent->d_name[0] not a valid number, skip it
                if (ent->d_name[0] > '9' || ent->d_name[0] < '0') {
                    continue;
                }

                // error when fork child process
                if (strstr(ent->d_name, "_err")) {
                    orte_vpid_t vpid = get_vpid_from_err_file(ent->d_name);
                    orte_proc_t* proc = opal_pointer_array_get_item(job->procs, vpid);
                    if (!proc) {
                        ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
                        goto error_out;
                    }
                    if (proc->state <= ORTE_PROC_STATE_UNTERMINATED) {
                        ORTE_ACTIVATE_PROC_STATE(&proc->name, ORTE_PROC_STATE_FAILED_TO_LAUNCH);
                    }
                    continue;
                }

                launched_proc_num++;

                // not error, get filename/length
                orte_vpid_t vpid = get_vpid_from_normal_file(ent->d_name);
                orte_proc_t* proc = opal_pointer_array_get_item(job->procs, vpid);
                if (!proc) {
                    ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
                    goto error_out;
                }

                // get length of file
                sprintf(full_file_name, "%s/%s", pid_dir, ent->d_name);

                struct stat st;
                if (0 != (rc = stat(full_file_name, &st))) {
                    // read file failed
                    opal_output(0, "%s odls:yarn:local_process_state_monitor_cb: failed to get size of pid file, path:%s",
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), full_file_name);
                    ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
                    goto error_out;
                }

                if (st.st_size > 0) {
                    // already finished
                    finished_proc_num++;

                    if (proc->state <= ORTE_PROC_STATE_UNTERMINATED) {
                        FILE * fp;
                        fp = fopen(full_file_name, "r");
                        if (fp == NULL) {
                            opal_output(0, "%s odls:yarn:local_process_state_monitor_cb: failed to open file, %s",
                                ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), full_file_name);
                            ORTE_ERROR_LOG(ORTE_ERR_FILE_READ_FAILURE);
                            goto error_out;
                        }
                        char line[1024];
                        if (!fgets(line, 1024, fp)) {
                            opal_output(0, "%s odls:yarn:local_process_state_monitor_cb: failed to read file content, %s",
                                ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), full_file_name);
                            ORTE_ERROR_LOG(ORTE_ERR_FILE_READ_FAILURE);
                            fclose(fp);
                            goto error_out;
                        }
                        fclose(fp);
                        int rc = atoi(line);
                        process_proc_exit_status(rc, proc);
                    }
                } else {
                    // started but not finished yet
                    if (proc->state <= ORTE_PROC_STATE_UNTERMINATED && proc->state < ORTE_PROC_STATE_RUNNING) {
                        proc->alive = true;
                        ORTE_ACTIVATE_PROC_STATE(&proc->name, ORTE_PROC_STATE_RUNNING);
                    }
                }
            }
        }
    }

    // check if we need to transfer job state / start sensor
    if (launched_proc_num >= orte_local_children->size) {
        if (sensor_started) {
            orte_sensor.start(jobid);
            sensor_started = true;
        }
        if (job->state <= ORTE_JOB_STATE_LAUNCH_APPS) {
            job->state = ORTE_JOB_STATE_LOCAL_LAUNCH_COMPLETE;
            ORTE_ACTIVATE_JOB_STATE(job, ORTE_JOB_STATE_LOCAL_LAUNCH_COMPLETE);
        }
    }

    if (finished_proc_num < orte_local_children->size) {
        // register another event if needed
        yarn_local_procs_monitor_t* mon = OBJ_NEW(yarn_local_procs_monitor_t);
        mon->jobid = caddy->jobid;
        opal_event_evtimer_set(orte_event_base, &mon->ev, local_process_state_monitor_cb, mon);
        opal_event_evtimer_add(&mon->ev, &tv);
    }

    // move on
    OBJ_RELEASE(caddy);
    return;

error_out:
    // release last caddy
    OBJ_RELEASE(caddy);
    if (!job) {
        ORTE_ACTIVATE_JOB_STATE(NULL, ORTE_JOB_STATE_FAILED_TO_LAUNCH);
    } else {
        ORTE_ACTIVATE_JOB_STATE(job, ORTE_JOB_STATE_FAILED_TO_LAUNCH);
    }
}

static void yarn_daemon_sync_recv(int status, orte_process_name_t* sender,
                           opal_buffer_t* buffer, orte_rml_tag_t tag,
                           void* cbdata)
{
    yarn_local_procs_monitor_t *mon;
    struct timeval tv = {1, 0};

    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                     "%s odls:yarn:yarn_daemon_sync_recv: recved sync response from hnp, start tracking proc state",
                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));

    /*
     * since local procs are launched by YARN NM, we here just to check
     * vpid and exit_code of each local procs with a timer libevent
     */
    mon = OBJ_NEW(yarn_local_procs_monitor_t);
    mon->jobid = jobid;
    opal_event_evtimer_set(orte_event_base, &mon->ev, local_process_state_monitor_cb, mon);
    opal_event_evtimer_add(&mon->ev, &tv);
}

static int create_dir_for_pidfile() {
    char dir_name[1024];
    strcpy(dir_name, "../");
    sprintf(dir_name + strlen(dir_name), "/%u", jobid);
    return mkdir(dir_name, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
}

/**
 * Launch all processes allocated to the current node.
 */
static int orte_odls_yarn_launch_local_procs(opal_buffer_t *data)
{
    int rc, i;
    opal_buffer_t *msg;

    /* construct the list of children we are to launch */
    if (ORTE_SUCCESS != (rc = orte_odls_base_default_construct_child_list(data, &jobid))) {
        opal_output(0, "%s odls:yarn:orte_odls_yarn_launch_local_procs: failed to construct child list on error %s",
            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), ORTE_ERROR_NAME(rc));
        ORTE_ERROR_LOG(rc);
        return rc;
    }

    /* mark all local children to alive so as to make xcast behave right */
    for (i = 0; i < orte_local_children->size; i++) {
        orte_proc_t* proc = opal_pointer_array_get_item(orte_local_children, i);
        if (!proc) {
            continue;
        }
        proc->alive = true;
    }

    /* create dir for pid file */
    if (ORTE_SUCCESS != (rc = create_dir_for_pidfile())) {
        opal_output(0, "%s odls:yarn:orte_odls_yarn_launch_local_procs, failed to create directory for pidfile",
        		ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
        ORTE_ERROR_LOG(rc);
    }

    /* register a callback for hnp sync response */
    orte_rml.recv_buffer_nb(ORTE_NAME_WILDCARD, ORTE_RML_TAG_YARN_SYNC_RESPONSE,
    		ORTE_RML_PERSISTENT, yarn_daemon_sync_recv, NULL);

    /* send a sync msg to hnp */
    msg = OBJ_NEW(opal_buffer_t);
    orte_rml.send_buffer_nb(ORTE_PROC_MY_HNP, msg, ORTE_RML_TAG_YARN_SYNC_REQUEST,
                                              orte_rml_send_callback, NULL);

    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                     "%s odls:yarn:orte_odls_yarn_launch_local_procs: finish send sync request to hnp, waitting for response ",
                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));

    return ORTE_SUCCESS;
}
