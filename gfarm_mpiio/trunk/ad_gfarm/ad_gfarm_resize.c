/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2001 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_gfarm.h"
#include "adioi.h"

void ADIOI_GFARM_Resize(ADIO_File fd, ADIO_Offset size, int *error_code)
{
    int myrank, nprocs;

    *error_code = MPI_SUCCESS;

    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);
#ifdef GFARM_DEBUG
	FPRINTF(stdout, "[%d/%d] ADIOI_GFARM_Resize called on %s\n", 
	    myrank, nprocs, fd->filename);
#endif
}
