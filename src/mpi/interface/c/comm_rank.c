/*
 * $HEADERS$
 */
#include "lam_config.h"
#include <stdio.h>

#include "mpi.h"
#include "mpi/interface/c/bindings.h"

#if LAM_HAVE_WEAK_SYMBOLS && LAM_PROFILING_DEFINES
#pragma weak MPI_Comm_rank = PMPI_Comm_rank
#endif


int MPI_Comm_rank(MPI_Comm comm, int *rank) {
    return MPI_SUCCESS;
}
