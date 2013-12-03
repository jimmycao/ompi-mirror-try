/*
 * Copyright (c) 2004-2008 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2011 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2006 The Regents of the University of California.
 *                         All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "ompi_config.h"
#include "ompi/constants.h"

#include "pubsub_hrte.h"

static int pubsub_hrte_component_register(void);
static int pubsub_hrte_component_open(void);
static int pubsub_hrte_component_close(void);
static int pubsub_hrte_component_query(mca_base_module_t **module, int *priority);

static int my_priority = 20;

ompi_pubsub_hrte_component_t mca_pubsub_hrte_component = {
    {
        /* First, the mca_base_component_t struct containing meta
           information about the component itself */

        {
          OMPI_PUBSUB_BASE_VERSION_2_0_0,
        
          "hrte", /* MCA component name */
          OMPI_MAJOR_VERSION,  /* MCA component major version */
          OMPI_MINOR_VERSION,  /* MCA component minor version */
          OMPI_RELEASE_VERSION,  /* MCA component release version */
          pubsub_hrte_component_open,  /* component open */
          pubsub_hrte_component_close, /* component close */
          pubsub_hrte_component_query, /* component query */
          pubsub_hrte_component_register /* component register */
        },
        {
            /* This component is checkpoint ready */
            MCA_BASE_METADATA_PARAM_CHECKPOINT
        }
    }
};

static int pubsub_hrte_component_register(void)
{
    my_priority = 20;
    (void) mca_base_component_var_register(&mca_pubsub_hrte_component.super.base_version,
                                           "priority", "Priority of the pubsub pmi component",
                                           MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                           OPAL_INFO_LVL_9,
                                           MCA_BASE_VAR_SCOPE_READONLY,
                                           &my_priority);

    mca_pubsub_hrte_component.server_uri = NULL;
    (void) mca_base_component_var_register(&mca_pubsub_hrte_component.super.base_version,
                                           "server", "Contact info for ompi_server for publish/subscribe operations",
                                           MCA_BASE_VAR_TYPE_STRING, NULL, 0, 0,
                                           OPAL_INFO_LVL_9,
                                           MCA_BASE_VAR_SCOPE_READONLY,
                                           &mca_pubsub_hrte_component.server_uri);

    return OMPI_SUCCESS;
}

static int pubsub_hrte_component_open(void)
{
    return OMPI_SUCCESS;
}

static int pubsub_hrte_component_close(void)
{
    return OMPI_SUCCESS;
}

static int pubsub_hrte_component_query(mca_base_module_t **module, int *priority)
{
    mca_pubsub_hrte_component.server_found = false;
    
    *priority = my_priority;
    *module = (mca_base_module_t *) &ompi_pubsub_hrte_module;
    return OMPI_SUCCESS;
}
