/*
 * $HEADERS$
 */
#include "lam_config.h"
#include <stdio.h>

#include "mpi.h"
#include "mpi/interface/c/bindings.h"

#if LAM_HAVE_WEAK_SYMBOLS && LAM_PROFILING_DEFINES
#pragma weak MPI_Comm_disconnect = PMPI_Comm_disconnect
#endif

int MPI_Comm_disconnect(MPI_Comm *comm) {
    return MPI_SUCCESS;
}
