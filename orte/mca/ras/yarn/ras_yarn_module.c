/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2011-2012 Los Alamos National Security, LLC.  All rights
 *                         reserved.
 * Copyright (c) 2013 Cisco Systems, Inc.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */
#include "orte_config.h"
#include "orte/constants.h"
#include "orte/types.h"

#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "opal/util/argv.h"
#include "opal/util/net.h"
#include "opal/util/if.h"
#include "opal/util/output.h"
#include "opal/opal_socket_errno.h"

#include "orte/util/show_help.h"
#include "orte/mca/errmgr/errmgr.h"
#include "orte/mca/rmaps/base/base.h"
#include "orte/util/name_fns.h"
#include "orte/runtime/orte_globals.h"

#include "orte/mca/ras/base/ras_private.h"
#include "opal/class/opal_bitmap.h"

#include "ras_yarn.h"

#include "orte/mca/common/hdclient.h"
#include "orte/mca/common/base/allocate.h"

/*
 * API functions
 */
static int orte_ras_yarn_allocate(orte_job_t *jdata, opal_list_t *nodes);
static int orte_ras_yarn_finalize(void);

/*
 * RAS yarn module
 */
orte_ras_base_module_t orte_ras_yarn_module = {
	NULL,
    orte_ras_yarn_allocate,
    NULL,
    orte_ras_yarn_finalize
};


/*
extern int allocate_resources_with_am(int resource_count, node_resource_t **node_resource_array, int *array_size);
 */

static int orte_ras_yarn_allocate_internal(int np, opal_list_t* nodes) {
    int rc, i;
    node_resource_t *node_resource_array = NULL;
    int array_size = 0;

    if (0 != allocate_resources_with_am(np, &node_resource_array, &array_size) || array_size < 0) {
    	opal_output(0, "%s orte_ras_yarn_allocate_internal: allocate resources with am failed, please check", ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
    	goto cleanup;
    }

    // read node resources
    for (i = 0; i < array_size; i++) {
    	//make node_t and add it to nodes
    	orte_node_t* node = OBJ_NEW(orte_node_t);
    	if (!node) {
    		opal_output(0, "%s ras:yarn: failed to create orte_node_t obj", ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
    		goto cleanup;
    	}

    	node->name = strdup(node_resource_array[i].host_name);
    	node->state = ORTE_NODE_STATE_UP;
    	node->slots_inuse = 0;
    	node->slots_max = 0;
    	node->slots = node_resource_array[i].slot_num;
    	opal_list_append(nodes, &node->super);

    	OPAL_OUTPUT_VERBOSE((5, orte_ras_base_framework.framework_output,
    	                     "%s ras:yarn: adding node %s with %d slot",
    	                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
    	                     node->name, node->slots));
    }

    // All done
    OPAL_OUTPUT_VERBOSE((1, orte_ras_base_framework.framework_output,
                         "%s ras:yarn:allocate: success",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));

    free_node_resource_array(node_resource_array, array_size);
    return ORTE_SUCCESS;

cleanup:
	free_node_resource_array(node_resource_array, array_size);
	return ORTE_ERROR;
}

//static int orte_ras_yarn_allocate(opal_list_t* nodes)
static int orte_ras_yarn_allocate(orte_job_t *jdata, opal_list_t *nodes)
{
    int i, j, rc;

    if (0 != (rc = orte_hdclient_connect_to_am())) {
        opal_output(0, "%s failed to connect to AM", ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
        return ORTE_ERROR;
    }

    // register to am first
    if (ORTE_SUCCESS != (rc = register_to_am())) {
        opal_output(0, "%s error when registering to AM", ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
        return ORTE_ERROR;
    }

    // set managed allocation
    orte_managed_allocation = true;

    // calculate all slots needed
    int slot_num = 0;
    for (i = 0; i < jdata->apps->size; i++) {
    	orte_app_context_t *app = NULL;
    	if (NULL == (app = (orte_app_context_t*)opal_pointer_array_get_item(jdata->apps, i))) {
    		continue;
    	}
    	slot_num += app->num_procs;
    }

    return orte_ras_yarn_allocate_internal(slot_num, nodes);
}

static int orte_ras_yarn_finalize(void) {
	return ORTE_SUCCESS;
}
