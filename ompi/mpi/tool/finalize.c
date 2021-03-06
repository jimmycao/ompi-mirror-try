/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2013 Los Alamos National Security, LLC. All rights
 *                         reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */


#include "ompi/mpi/tool/mpit-internal.h"

#include "opal/include/opal/sys/atomic.h"
#include "opal/runtime/opal.h"

#if OPAL_HAVE_WEAK_SYMBOLS && OMPI_PROFILING_DEFINES
#pragma weak MPI_T_finalize = PMPI_T_finalize
#endif

#if OMPI_PROFILING_DEFINES
#include "ompi/mpi/tool/profile/defines.h"
#endif

static const char FUNC_NAME[] = "MPI_T_finalize";

int MPI_T_finalize (void)
{
    mpit_lock ();

    if (!mpit_is_initialized ()) {
        mpit_unlock ();
        return MPI_T_ERR_NOT_INITIALIZED;
    }

    if (0 == --mpit_init_count) {
        (void) opal_finalize_util ();
    }

    mpit_unlock ();

    return MPI_SUCCESS;
}
