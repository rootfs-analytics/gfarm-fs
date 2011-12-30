/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2001 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#ifndef AD_GFARM_INCLUDE
#define AD_GFARM_INCLUDE

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <math.h>
#include "adio.h"
#include <gfarm/gfarm.h>


typedef struct {  
    char *filename;
    int nprocs;
    int *count;
    unsigned *filetype_size;
    MPI_Aint *filetype_extent;
    ADIO_Offset **blocklens;      /* array of contiguous block lengths (bytes)*/
    ADIO_Offset **indices;        /* array of byte offsets of each block */
    int gfp_num;
    int gfp_index;
    int *gfp_flag;
    unsigned long *data_size;
    double all_time;
    double access_time;
    GFS_File *gfp;
    GFS_File header_gfp;
    int is_collective;
    int newfile;
	int header_flag;
	int gfarm_view_flag;
    struct ADIOI_GFARM_col_info *gci;
} ADIOI_GFARM_headerInf;

typedef struct ADIOI_GFARM_col_info{  
	int same_rank_count;
	int head_rank;
	int head_rank_flag;
	int *same_rank_arr;
	GFS_File *gfp;
}ADIOI_GFARM_collective_info;



void ADIOI_GFARM_Open(ADIO_File fd, int *error_code);
void ADIOI_GFARM_Close(ADIO_File fd, int *error_code);
void ADIOI_GFARM_ReadContig(ADIO_File fd, void *buf, int count, 
			     MPI_Datatype datatype, int file_ptr_type,
			     ADIO_Offset offset, ADIO_Status *status, int
			     *error_code);
void ADIOI_GFARM_WriteContig(ADIO_File fd, void *buf, int count, 
			      MPI_Datatype datatype, int file_ptr_type,
			      ADIO_Offset offset, ADIO_Status *status, int
			      *error_code);   
void ADIOI_GFARM_IwriteContig(ADIO_File fd, void *buf, int count, 
			       MPI_Datatype datatype, int file_ptr_type,
			       ADIO_Offset offset, ADIO_Request *request, int
			       *error_code);   
void ADIOI_GFARM_IreadContig(ADIO_File fd, void *buf, int count, 
			      MPI_Datatype datatype, int file_ptr_type,
			      ADIO_Offset offset, ADIO_Request *request, int
			      *error_code);   
int ADIOI_GFARM_ReadDone(ADIO_Request *request, ADIO_Status *status, int
			  *error_code);
int ADIOI_GFARM_WriteDone(ADIO_Request *request, ADIO_Status *status, int
			   *error_code);
void ADIOI_GFARM_ReadComplete(ADIO_Request *request, ADIO_Status *status, int
			       *error_code); 
void ADIOI_GFARM_WriteComplete(ADIO_Request *request, ADIO_Status *status,
				int *error_code); 
void ADIOI_GFARM_Fcntl(ADIO_File fd, int flag, ADIO_Fcntl_t *fcntl_struct, 
			int *error_code); 
void ADIOI_GFARM_WriteStrided(ADIO_File fd, void *buf, int count,
			       MPI_Datatype datatype, int file_ptr_type,
			       ADIO_Offset offset, ADIO_Status *status,
			       int *error_code);
void ADIOI_GFARM_ReadStrided(ADIO_File fd, void *buf, int count,
			      MPI_Datatype datatype, int file_ptr_type,
			      ADIO_Offset offset, ADIO_Status *status, int
			      *error_code);
void ADIOI_GFARM_WriteStridedColl(ADIO_File fd, void *buf, int count,
				   MPI_Datatype datatype, int file_ptr_type,
				   ADIO_Offset offset, ADIO_Status *status, int
				   *error_code);
void ADIOI_GFARM_ReadStridedColl(ADIO_File fd, void *buf, int count,
				  MPI_Datatype datatype, int file_ptr_type,
				  ADIO_Offset offset, ADIO_Status *status, int
				  *error_code);
void ADIOI_GFARM_IreadStrided(ADIO_File fd, void *buf, int count,
			       MPI_Datatype datatype, int file_ptr_type,
			       ADIO_Offset offset, ADIO_Request *request, int
			       *error_code);
void ADIOI_GFARM_IwriteStrided(ADIO_File fd, void *buf, int count,
				MPI_Datatype datatype, int file_ptr_type,
				ADIO_Offset offset, ADIO_Request *request, int
				*error_code);
void ADIOI_GFARM_Flush(ADIO_File fd, int *error_code);
void ADIOI_GFARM_Resize(ADIO_File fd, ADIO_Offset size, int *error_code);
ADIO_Offset ADIOI_GFARM_SeekIndividual(ADIO_File fd, ADIO_Offset offset, 
					int whence, int *error_code);
void ADIOI_GFARM_SetInfo(ADIO_File fd, MPI_Info users_info, int *error_code);
void ADIOI_GFARM_Get_shared_fp(ADIO_File fd, int size, 
				ADIO_Offset *shared_fp, 
				int *error_code);
void ADIOI_GFARM_Set_shared_fp(ADIO_File fd, ADIO_Offset offset, 
				int *error_code);
void ADIOI_GFARM_Delete(char *filename, int *error_code);



#endif
