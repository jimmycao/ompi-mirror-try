/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University.
 *                         All rights reserved.
 * Copyright (c) 2004-2005 The Trustees of the University of Tennessee.
 *                         All rights reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "ompi_config.h"
#include "datatype/datatype.h"
#include "datatype/datatype_internal.h"

int ompi_ddt_destroy( dt_desc_t** dt )
{
   dt_desc_t* pData = *dt;

   if( pData->flags & DT_FLAG_FOREVER )
      return OMPI_ERROR;

   OBJ_RELEASE( pData );
   *dt = NULL;
   return OMPI_SUCCESS;
}
