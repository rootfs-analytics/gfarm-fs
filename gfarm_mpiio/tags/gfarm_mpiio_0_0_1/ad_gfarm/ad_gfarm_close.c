/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2001 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_gfarm.h"

void ADIOI_GFARM_Close(ADIO_File fd, int *error_code)
{
    int myrank, nprocs;
    int i, n;
    ADIOI_GFARM_headerInf *ghi;
    ADIOI_GFARM_collective_info *gci;
    gfarm_error_t gerr;
    
    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);
    
    fd->fd_sys = -1;
    *error_code = MPI_SUCCESS;
    
#ifdef GFARM_DEBUG
    FPRINTF(stdout, "[%d/%d] ADIOI_GFARM_Close called on %s\n", myrank, 
	    nprocs, fd->filename);
#endif

    
   	ghi = (ADIOI_GFARM_headerInf *)(fd->fs_ptr);
    if(ghi->gfarm_view_flag == 1){
		for(i=0;i<ghi->gfp_num;i++){
			gfs_pio_close(ghi->gfp[i]);
		}
		gfs_pio_close(ghi->header_gfp);
    	
#ifdef GFARM_DEBUG_TIME
ad_gfarm_print_detailaccesstime(fd);
#endif
#ifdef GFARM_DEBUG_DATA_SIZE
ad_gfarm_print_detaildatasize(fd);
#endif

    	free(ghi->count);
    	free(ghi->filetype_size);
    	free(ghi->filetype_extent);
	for(i=0;i<ghi->nprocs;i++){
		free(ghi->blocklens[i]);
		free(ghi->indices[i]);
	}
    	free(ghi->blocklens);
    	free(ghi->indices);
    	//free(ghi->gfp_flag);
    	if(ghi->gfp_num > 0)	free(ghi->gfp);
		free(ghi);
	}else if(ghi->header_flag == 1){
			gfs_pio_close(ghi->header_gfp);
	}
	
	gerr = gfarm_terminate();
	if(gerr != GFARM_ERR_NO_ERROR){
	    printf("[%d/%d] gfarm_initialize. gerr = %d\n", myrank, nprocs, gerr);
   	}

#ifdef GFARM_DEBUG
    FPRINTF(stdout, "[%d/%d] end ADIOI_GFARM_Close called on %s\n", myrank, 
	    nprocs, fd->filename);
#endif
}


void ad_gfarm_print_detailaccesstime(ADIO_File fd)
{

	int i, myrank, nprocs;
	double *trance_time_arr, *access_time_arr, *all_time_arr;
	double trance_time;

	ADIOI_GFARM_headerInf *ghi;
	ghi = (ADIOI_GFARM_headerInf*)fd->fs_ptr;

	MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);
	
	trance_time_arr = malloc(sizeof(double)*nprocs);
	access_time_arr = malloc(sizeof(double)*nprocs);
	all_time_arr = malloc(sizeof(double)*nprocs);

	trance_time = ghi->all_time - ghi->access_time;

	MPI_Gather(&trance_time, 1, MPI_DOUBLE, trance_time_arr, 1, MPI_DOUBLE, 0, fd->comm);
	MPI_Gather(&(ghi->access_time), 1, MPI_DOUBLE, access_time_arr, 1, MPI_DOUBLE, 0, fd->comm);
	MPI_Gather(&(ghi->all_time), 1, MPI_DOUBLE, all_time_arr, 1, MPI_DOUBLE, 0, fd->comm);

	if(myrank == 0){
		FILE *fp;
		fp = fopen("result/time_gfarm", "a+");
		fprintf(fp, "%d\n", nprocs);
			for(i=0;i<nprocs;i++){
				fprintf(fp, "%f\t", all_time_arr[i]);
				fprintf(fp, "%f\t", access_time_arr[i]);
				fprintf(fp, "%f\t", trance_time_arr[i]);
				fprintf(fp, "%f\t", (trance_time_arr[i]/all_time_arr[i])*100);
				fprintf(fp, "\n");
			}
		fprintf(fp, "\n");
		fclose(fp);

	}
	free(trance_time_arr);
	free(access_time_arr);
	free(all_time_arr);
}

void ad_gfarm_print_detaildatasize(ADIO_File fd)
{
	int i, n, myrank, nprocs;
	unsigned long *data_size_arr;
	unsigned long data_size_sum;
	double data_size_rate, *data_size_rate_arr;

	ADIOI_GFARM_headerInf *ghi;
	ghi = (ADIOI_GFARM_headerInf*)fd->fs_ptr;

	MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);

	data_size_arr = malloc(sizeof(unsigned long)*ghi->nprocs*nprocs);
	data_size_rate_arr = malloc(sizeof(double)*nprocs);
	MPI_Gather(ghi->data_size, ghi->nprocs, MPI_UNSIGNED_LONG, data_size_arr, ghi->nprocs, MPI_UNSIGNED_LONG, 0, fd->comm);
	data_size_sum = 0;
	for(i=0;i<ghi->nprocs;i++){
		data_size_sum += ghi->data_size[i];
	}

	if(data_size_sum!=0){
		data_size_rate = ((double)ghi->data_size[myrank])/((double)data_size_sum);
	}else{
		data_size_rate = 0;
	}
	
	MPI_Gather(&data_size_rate, 1, MPI_DOUBLE, data_size_rate_arr, 1, MPI_DOUBLE, 0, fd->comm);
	if(myrank == 0){
		FILE *fp;
		fp = fopen("result/data_size_gfarm", "a+");
		fprintf(fp, "%d\n", nprocs);
		for(n=0;n<nprocs;n++){
			fprintf(fp, "%d\t", n);
			for(i=0;i<ghi->nprocs;i++){
				fprintf(fp, "%d\t", data_size_arr[ghi->nprocs*n+i]);
			}
			fprintf(fp, "\n");
		}
		fprintf(fp, "\n");
		fclose(fp);
		fp = fopen("result/data_rate_gfarm", "a+");
		fprintf(fp, "%d\n", nprocs);
		for(n=0;n<nprocs;n++){
			fprintf(fp, "%d\t", n);
			fprintf(fp, "%f\t", data_size_rate_arr[n]);
			fprintf(fp, "\n");
		}
		fprintf(fp, "\n");
		fclose(fp);

	}
}

