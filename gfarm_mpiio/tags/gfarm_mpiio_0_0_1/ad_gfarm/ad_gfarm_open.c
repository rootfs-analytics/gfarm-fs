/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2001 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_gfarm.h"
#include "adioi.h"

void ADIOI_GFARM_Open(ADIO_File fd, int *error_code)
{
    int myrank, nprocs;
	size_t size;
    char *gfarm_dirname, *gfarm_meta_dirname, *gfarm_data_dirname, *gfarm_filename, *getvalue;


    ADIOI_GFARM_headerInf *ghi;
    
    GFS_Dir gfs_dirp;
    GFS_File gfs_file;
	gfarm_error_t gerr;

    fd->fd_sys = 1;
    fd->fd_direct = -1;
    *error_code = MPI_SUCCESS;

    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);
    
#ifdef GFARM_DEBUG
    FPRINTF(stdout, "[%d/%d] ADIOI_GFARM_Open called on %s\n", myrank, 
	    nprocs, fd->filename);
#endif

    gerr = gfarm_initialize(NULL, NULL);
    if(gerr != GFARM_ERR_NO_ERROR){
	    printf("[%d/%d] gfarm_initialize. gerr = %d\n", myrank, nprocs, gerr);
    }

    //MPI_INFO = NULL
    gerr = gfs_opendir(fd->filename, &gfs_dirp);
    //create new file
    if(gerr == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY){
			//create dirname 
			gfarm_meta_dirname = (char*)malloc(sizeof(char)*(strlen(fd->filename)+10));
			gfarm_data_dirname = (char*)malloc(sizeof(char)*(strlen(fd->filename)+10));
			gfarm_filename = (char*)malloc(sizeof(char)*(strlen(fd->filename)+10));
			sprintf(gfarm_meta_dirname, "%s/meta", fd->filename);
			sprintf(gfarm_data_dirname, "%s/data", fd->filename);
			sprintf(gfarm_filename, "%s/data/0", fd->filename);
				//create dir
			if(myrank == 0){
				//root dir
				if(gerr = gfs_mkdir(fd->filename, 0000755) != GFARM_ERR_NO_ERROR){
					printf("[%d/%d] gfs_mkdir %s. gerr = %d\n", myrank, nprocs, fd->filename, gerr);
				}
				//set mpi-io/gfarm flag
				if(gerr = gfs_setxattr(fd->filename, "mpiio", "1", 16, GFS_XATTR_CREATE)
					!= GFARM_ERR_NO_ERROR){
					printf("[%d/%d] gfs_setxattr %s. gerr = %d\n", myrank, nprocs, fd->filename, gerr);
				}
				//meta dir
				if(gerr = gfs_mkdir(gfarm_meta_dirname, 0000755) != GFARM_ERR_NO_ERROR){
					printf("[%d/%d] gfs_mkdir %s. gerr = %d\n", myrank, nprocs, gfarm_meta_dirname, gerr);
				}
				//data dir
				if(gerr = gfs_mkdir(gfarm_data_dirname, 0000755) != GFARM_ERR_NO_ERROR){
					printf("[%d/%d] gfs_mkdir %s. gerr = %d\n", myrank, nprocs, gfarm_data_dirname, gerr);
				}

				//create file
				if(gerr = gfs_pio_create(gfarm_filename,GFARM_FILE_RDWR,0000644,&gfs_file)){
					printf("[%d/%d] gfs_pio_create %s. gerr = %d\n", myrank, nprocs, gfarm_filename, gerr);
				}
			}
		    ghi = (ADIOI_GFARM_headerInf*)malloc(sizeof(ADIOI_GFARM_headerInf));
			ghi->header_flag = 1;
			ghi->gfarm_view_flag = 0;
			ghi->header_gfp = gfs_file;
			fd->fs_ptr = ghi;

			free(gfarm_meta_dirname);
			free(gfarm_data_dirname);
			free(gfarm_filename);
    //access split file
    }else if(gerr == GFARM_ERR_NO_ERROR){
	    gfs_closedir(gfs_dirp);
	    getvalue = (char*)malloc(sizeof(char)*16);
		size = 16;
		gerr = gfs_getxattr(fd->filename, "mpiio", getvalue, &size);
	    if(gerr != GFARM_ERR_NO_ERROR){
		    //error_code
			printf("[%d/%d] gfarm_open. getvalue = %s, size = %d\n", myrank, nprocs, getvalue, size);
			printf("[%d/%d] gfarm_open %s is directory. but it is not set to mpiio flag. size = %d, gerr = %d\n", myrank, nprocs, fd->filename, size, gerr);
		    //return;
	    }

	    //first open or not
	    if(atoi(getvalue) == 1){
		    gfarm_filename = (char*)malloc(sizeof(char)*(strlen(fd->filename)+10));
		    sprintf(gfarm_filename, "%s/data/0", fd->filename);
		    //file open
		    gerr = gfs_pio_open(gfarm_filename, GFARM_FILE_RDWR, &gfs_file);
		    if(gerr != GFARM_ERR_NO_ERROR){
			    //error_code
				printf("[%d/%d] gfarm_open. error in open header file. gerr = %d\n", myrank, nprocs, gerr);
			    return;
		    }

			if(fd->fs_ptr != NULL){
				ghi = (ADIOI_GFARM_headerInf*)fd->fs_ptr;
			}else{
				ghi = (ADIOI_GFARM_headerInf*)malloc(sizeof(ADIOI_GFARM_headerInf));
				fd->fs_ptr = ghi;
			}
		    ghi->header_gfp = gfs_file;
		    ghi->header_flag = 1;
			ghi->gfarm_view_flag = 0;
			ghi->gfp_num = 0;

			free(gfarm_filename);
	    }else if(atoi(getvalue) == 2){
			printf("[%d/%d] gfarm_open xattr.mpiio = %c. access to a existed file.\n", myrank, nprocs, getvalue);
	    }

		free(getvalue);
    
    //error
    }else{
	    //error_code
	    printf("[%d/%d] gerr = %d.\n", myrank, nprocs, gerr);
	    return;
    }
    error_code = MPI_SUCCESS; 
#ifdef GFARM_DEBUG
    FPRINTF(stdout, "[%d/%d] ADIOI_GFARM_Open end on %s\n", myrank, 
	    nprocs, fd->filename);
#endif

}
