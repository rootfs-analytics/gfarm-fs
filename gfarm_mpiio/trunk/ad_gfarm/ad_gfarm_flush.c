/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2001 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_gfarm.h"
#include "adioi.h"
#include "adio_extern.h"

void ADIOI_GFARM_Flush(ADIO_File fd, int *error_code)
{
    int myrank, nprocs;
    int i;
    ADIOI_GFARM_headerInf *ghi;
    ADIOI_GFARM_collective_info *gci;

    *error_code = MPI_SUCCESS;
    ghi = (ADIOI_GFARM_headerInf *)(fd->fs_ptr);
    if(ghi->gfarm_view_flag == 1){
    	for(i=0;i<ghi->gfp_num;i++){
    		gfs_pio_datasync(ghi->gfp[i]);
    	}
		gfs_pio_datasync(ghi->header_gfp);
    	if(ghi->is_collective == 1){
    		gci = (ADIOI_GFARM_collective_info *)ghi->gci;
    		if(gci->head_rank_flag == 1 && gci->gfp != NULL){
    			for(i=0;i<((gci->same_rank_count)-1);i++){
    				gfs_pio_datasync(gci->gfp[i]);
    			}
    		}
    	}
    }else if(ghi->gfarm_view_flag == 2){
    	ghi = (ADIOI_GFARM_headerInf *)(fd->fs_ptr);
    	gfs_pio_datasync(ghi->gfp[0]);
    }else{
    
    }
    
#ifdef GFARM_DEBUG
    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);
    FPRINTF(stdout, "[%d/%d] ADIOI_GFARM_Flush called on %s\n", 
	    myrank, nprocs, fd->filename);
#endif

}
