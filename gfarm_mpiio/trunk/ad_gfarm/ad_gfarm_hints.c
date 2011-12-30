/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2001 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_gfarm.h"
#include "adioi.h"
#ifdef ROMIO_BGL
#include "../ad_bgl/ad_bgl.h"
#endif
void ADIOI_GFARM_SetInfo(ADIO_File fd, MPI_Info users_info, int *error_code)
{
    int myrank, nprocs;

    *error_code = MPI_SUCCESS;

#ifdef GFARM_DEBUG
    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);
    FPRINTF(stdout, "[%d/%d] ADIOI_GFARM_SetInfo called on %s\n", 
	    myrank, nprocs, fd->filename);
    FPRINTF(stdout, "[%d/%d]    calling ADIOI_GEN_SetInfo\n", 
	    myrank, nprocs);
#endif

    ADIOI_GEN_SetInfo(fd, users_info, error_code);
#ifdef GFARM_DEBUG
    printf("end gfarm_setinfo\n");
#endif
}
