/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2001 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_gfarm.h"
#include "adioi.h"

void ADIOI_GFARM_WriteStridedColl(ADIO_File fd, void *buf, int count,
				   MPI_Datatype datatype, int file_ptr_type,
				   ADIO_Offset offset, ADIO_Status *status, 
				   int *error_code)
{
    int myrank, nprocs;

    *error_code = MPI_SUCCESS;

#ifdef GFARM_DEBUG
    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);

    FPRINTF(stdout, "[%d/%d] ADIOI_GFARM_WriteStridedColl called on %s\n", 
	    myrank, nprocs, fd->filename);
    FPRINTF(stdout, "[%d/%d]    calling ADIOI_GFARM_WriteStrided\n", 
	    myrank, nprocs);
#endif

    ADIOI_GFARM_WriteStrided(fd, buf, count, datatype, file_ptr_type,
			       offset, status, error_code);
	//MPI_File_sync(fd);
}
