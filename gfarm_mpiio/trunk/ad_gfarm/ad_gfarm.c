/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2001 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_gfarm.h"

/* adioi.h has the ADIOI_Fns_struct define */
#include "adioi.h"
#include "adio_extern.h"

struct ADIOI_Fns_struct ADIO_GFARM_operations = {
    ADIOI_GFARM_Open, /* Open */
    ADIOI_GEN_OpenColl, /* OpenColl */
    ADIOI_GFARM_ReadContig, /* ReadContig */
    ADIOI_GFARM_WriteContig, /* WriteContig */
    ADIOI_GFARM_ReadStridedColl, /* ReadStridedColl */
    ADIOI_GFARM_WriteStridedColl, /* WriteStridedColl */
    ADIOI_GFARM_SeekIndividual, /* SeekIndividual */
    ADIOI_GFARM_Fcntl, /* Fcntl */
    ADIOI_GFARM_SetInfo, /* SetInfo */
    ADIOI_GFARM_ReadStrided, /* ReadStrided */
    ADIOI_GFARM_WriteStrided, /* WriteStrided */
    ADIOI_GFARM_Close, /* Close */
    ADIOI_FAKE_IreadContig, /* IreadContig */
    ADIOI_FAKE_IwriteContig, /* IwriteContig */
    ADIOI_FAKE_IODone, /* ReadDone */
    ADIOI_FAKE_IODone, /* ReadDone */
    ADIOI_FAKE_IOComplete, /* ReadComplete */
    ADIOI_FAKE_IOComplete, /* ReadComplete */
    ADIOI_FAKE_IreadStrided, /* IreadStrided */
    ADIOI_FAKE_IwriteStrided, /* IwriteStrided */
    ADIOI_GFARM_Flush, /* Flush */
    ADIOI_GFARM_Resize, /* Resize */
    ADIOI_GFARM_Delete, /* Delete */
    ADIOI_GEN_Feature, /* Features */
};

void ad_gfarm_set_view(ADIO_File fd, ADIO_Offset disp, MPI_Info info)
{
	int i,j,k;
	int read_only, write_only, number_of_set_view, overwrap;
	char tmp_filename[256];
	char tmp_dirname[256], tmp_metadirname[256], gfarm_filename[256];
	char *gfarm_header_file, *gfarm_file_name;
	char header_file_str[256], tmp_hfs;
	int myrank, nprocs;
	int *flat_file_count, flat_file_maxcount;
	int header_nprocs, *count_arr, flag;
	long header_seek_len;
	ADIO_Offset *header_disps, *disps;
	MPI_Aint *adds;
	MPI_Datatype *etypes, *filetypes, *header_etypes, *header_filetypes;
	MPI_Offset **flat_file_blocklens, **flat_file_indices, *flat_file_disp, tmp_indices;
	int header_fp;
	unsigned *filetype_size_arr, filetype_size;
	MPI_Aint *filetype_extent_arr, filetype_extent;
	mode_t permisson;
	ADIOI_Flatlist_node *flat_file;
	char hostname[256];
	int filetype_is_contig;
	
	//gfarm param
	GFS_File gfp, data_gfp;
	gfarm_off_t result, tail_header_file_offset;
	gfarm_error_t gerr;
	int g_np;
	GFS_Dir dirp;
	ADIOI_GFARM_headerInf *ghi;
	ghi = (ADIOI_GFARM_headerInf*)fd->fs_ptr;

	MPI_Comm_rank(fd->comm, &myrank);
	MPI_Comm_size(fd->comm, &nprocs);
	//gethostname(hostname,256);


#ifdef GFARM_DEBUG
	printf("[%d/%d]    ADIO_Set_view\n", myrank, nprocs);
#endif

	ADIOI_Datatype_iscontig(fd->filetype, &filetype_is_contig);
	if(filetype_is_contig){
#ifdef GFARM_DEBUG
	printf("[%d/%d] gfarm_set_view filtype_is_contig\n", myrank,nprocs);
#endif
		if(myrank < 10){
				gfarm_header_file = (char*)malloc(sizeof(char)*(strlen(fd->filename)+10+1));
		}else{
				gfarm_header_file = (char*)malloc(sizeof(char)*(strlen(fd->filename)+10+(int)log10(myrank)+1));
		}
		sprintf(gfarm_header_file, "%s/meta/%d", fd->filename, myrank);
		printf("[%d/%d] gfarm_headr_file = %s.\n", myrank, nprocs, gfarm_header_file);

		gerr = gfs_pio_open(gfarm_header_file, GFARM_FILE_RDWR, &gfp);
		printf("[%d/%d] open header_file. gerr  = %d.\n", myrank, nprocs, gerr);
		if(gerr == GFARM_ERR_NO_ERROR){
			gerr = gfs_pio_read(gfp, &number_of_set_view, sizeof(int), &g_np);
			if(gerr != GFARM_ERR_NO_ERROR){
					//error_code
					return;
			}
			gerr = gfs_pio_read(gfp, &tail_header_file_offset, sizeof(int), &g_np);
			if(gerr != GFARM_ERR_NO_ERROR){
					//error_code
					return;
			}
			number_of_set_view += 1;
			gerr = gfs_pio_seek(gfp, 0, 0, &result);
			if(gerr != GFARM_ERR_NO_ERROR){
					//error_code
					return;
			}
			gerr = gfs_pio_write(gfp, &number_of_set_view, sizeof(int), &g_np);
			if(gerr != GFARM_ERR_NO_ERROR){
					//error_code
					return;
			}
			gerr = gfs_pio_seek(gfp, tail_header_file_offset, 0, &result);
			if(gerr != GFARM_ERR_NO_ERROR){
					//error_code
					return;
			}
		}else if(gerr == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY){
			number_of_set_view = 0;
			gerr = gfs_pio_create(gfarm_header_file, GFARM_FILE_RDWR, 0000644, &gfp);
			if(gerr != GFARM_ERR_NO_ERROR){
					//error_code
					return;
			}
			number_of_set_view += 1;
			gerr = gfs_pio_write(gfp, &number_of_set_view, sizeof(int), &g_np);
			if(gerr != GFARM_ERR_NO_ERROR){
					//error_code
					return;
			}
		}else{
			//error_code 
			return;
		}

		int tmp_count;
		tmp_count = 0;
		gerr = gfs_pio_write(gfp, &nprocs, sizeof(int), &g_np);
		gerr = gfs_pio_write(gfp, &tmp_count, sizeof(int), &g_np);
		gerr = gfs_pio_write(gfp, &disp, sizeof(ADIO_Offset), &g_np);
		gerr = gfs_pio_seek(gfp, 0, 1, &tail_header_file_offset);
		gerr = gfs_pio_seek(gfp, 4, 0, &result);
		gerr = gfs_pio_write(gfp, &tail_header_file_offset, sizeof(int), &g_np);

		gfs_pio_close(gfp);
		
		if(myrank < 10){
			gfarm_file_name = (char*)malloc(sizeof(char)*
				(strlen(fd->filename)+10+1+(int)log10(number_of_set_view+1)));	
		}else{
			gfarm_file_name = (char*)malloc(sizeof(char)*
				(strlen(fd->filename)+10+(int)log10(myrank)+(int)log10(number_of_set_view+1)));	
		}
		sprintf(gfarm_file_name, "%s/data/0-%d", fd->filename, number_of_set_view-1);
		if(myrank == 0){
			gerr = gfs_pio_create(gfarm_file_name, GFARM_FILE_RDWR, 0000644, &data_gfp);
			gerr = gfs_pio_close(data_gfp);
		}
		MPI_Barrier(fd->comm);
		gerr = gfs_pio_open(gfarm_file_name, GFARM_FILE_RDWR, &data_gfp);
		if(ghi->gfp_num > 0){
			for(i=0;i<ghi->gfp_num;i++){
				gfs_pio_close(ghi->gfp[i]);
			}
			free(ghi->gfp);
		}
		ghi->gfp_num = 1;
		ghi->gfp_index = 0;
		ghi->gfp = (GFS_File*)malloc(sizeof(GFS_File));
		ghi->gfp[0] = data_gfp;

		ghi->nprocs = nprocs;
		ghi->count = 0;
		ghi->is_collective = 0;
					
		return;
	}else{
		flat_file = ADIOI_Flatlist;
		while (flat_file->type != fd->filetype) flat_file = flat_file->next;
	}
	
	
	if((gerr = gfs_opendir(fd->filename, &dirp)) == GFARM_ERR_NO_ERROR){
			//
			gfs_closedir(dirp);

			if(1){//ghi->header_flag == 1 && ghi->gfarm_view_flag == 0){
					if(myrank < 10){
						gfarm_header_file = (char*)malloc(sizeof(char)*(strlen(fd->filename)+10+1));
					}else{
						gfarm_header_file = (char*)malloc(sizeof(char)*(strlen(fd->filename)+10+(int)log10(myrank)+1));
					}
					sprintf(gfarm_header_file, "%s/meta/%d", fd->filename, myrank);

					gerr = gfs_pio_open(gfarm_header_file, GFARM_FILE_RDWR, &gfp);
					if(gerr == GFARM_ERR_NO_ERROR){
							gerr = gfs_pio_read(gfp, &number_of_set_view, sizeof(int), &g_np);
							if(gerr != GFARM_ERR_NO_ERROR){
									//error_code
									return;
							}
							gerr = gfs_pio_read(gfp, &tail_header_file_offset, sizeof(int), &g_np);
							if(gerr != GFARM_ERR_NO_ERROR){
									//error_code
									return;
							}
							number_of_set_view += 1;
							gerr = gfs_pio_seek(gfp, 0, 0, &result);
							if(gerr != GFARM_ERR_NO_ERROR){
									//error_code
									return;
							}
							gerr = gfs_pio_write(gfp, &number_of_set_view, sizeof(int), &g_np);
							if(gerr != GFARM_ERR_NO_ERROR){
									//error_code
									return;
							}
							gerr = gfs_pio_seek(gfp, tail_header_file_offset, 0, &result);
							if(gerr != GFARM_ERR_NO_ERROR){
									//error_code
									return;
							}
					}else if(gerr == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY){
							number_of_set_view = 0;
							gerr = gfs_pio_create(gfarm_header_file, GFARM_FILE_RDWR, 0000644, &gfp);
							if(gerr != GFARM_ERR_NO_ERROR){
									//error_code
									return;
							}
							number_of_set_view += 1;
							gerr = gfs_pio_write(gfp, &number_of_set_view, sizeof(int), &g_np);
							if(gerr != GFARM_ERR_NO_ERROR){
									//error_code
									return;
							}
					}else{
							//error_code 
							return;
					}
					
					MPI_Type_extent(fd->filetype, &filetype_extent);
					MPI_Type_size(fd->filetype, &filetype_size);

					//check overwrap reagion
					overwrap = ad_gfarm_check_view(fd, &filetype_extent, &filetype_size);

					//create new file
					if(overwrap == 0){
						if(myrank < 10){
								gfarm_file_name = (char*)malloc(sizeof(char)*
											(strlen(fd->filename)+10+1+(int)log10(number_of_set_view+1)));	
						}else{
								gfarm_file_name = (char*)malloc(sizeof(char)*
											(strlen(fd->filename)+10+(int)log10(myrank)+(int)log10(number_of_set_view+1)));	
						}
						sprintf(gfarm_file_name, "%s/data/%d-%d", fd->filename, myrank, number_of_set_view-1);
						gerr = gfs_pio_create(gfarm_file_name, GFARM_FILE_RDWR, 0000644, &data_gfp);
						if(gerr != GFARM_ERR_NO_ERROR){
							printf("[%d/%d] create error! gfarm_file_name = %s\n", myrank, nprocs, gfarm_file_name);
						}
							
						if(ghi->gfp_num > 0){
								for(i=0;i<ghi->gfp_num;i++){
										gfs_pio_close(ghi->gfp[i]);
								}
								free(ghi->gfp);
						}
						ghi->gfp_num = 1;
						ghi->gfp_index = 0;
						ghi->gfp = (GFS_File*)malloc(sizeof(GFS_File));
						//ghi->gfp_flag = (int*)malloc(sizeof(int)*gfp_num);
						ghi->gfp[0] = data_gfp;
						ghi->gfarm_view_flag = 1;

						ghi->is_collective = 0;
					}else{

					}
					//MPI_Type_extent(fd->etype, &etype_extent);
					//MPI_Type_size(fd->etype, &etype_extent);

					gerr = gfs_pio_write(gfp, &nprocs, sizeof(int), &g_np);
					gerr = gfs_pio_write(gfp, &(flat_file->count), sizeof(int), &g_np);
					gerr = gfs_pio_write(gfp, &disp, sizeof(ADIO_Offset), &g_np);
					gerr = gfs_pio_write(gfp, &filetype_size, sizeof(unsigned), &g_np);
					gerr = gfs_pio_write(gfp, &filetype_extent, sizeof(MPI_Aint), &g_np);
					gerr = gfs_pio_write(gfp, flat_file->blocklens, sizeof(ADIO_Offset)*(flat_file->count), &g_np);
					gerr = gfs_pio_write(gfp, flat_file->indices, sizeof(ADIO_Offset)*(flat_file->count), &g_np);

					gerr = gfs_pio_seek(gfp, 0, 1, &tail_header_file_offset);
					gerr = gfs_pio_seek(gfp, 4, 0, &result);
					gerr = gfs_pio_write(gfp, &tail_header_file_offset, sizeof(int), &g_np);

					gfs_pio_close(gfp);
#ifdef GFARM_DEBUG
	printf("[%d/%d] end gfarm_set_view\n", myrank,nprocs);
#endif


			}else{

					strcpy(tmp_metadirname, tmp_dirname);
					strcat(tmp_metadirname, "/meta");
					sprintf(gfarm_header_file, "%s/%d", tmp_metadirname, myrank);	
					
					
					if((gerr = gfs_pio_open(gfarm_header_file, GFARM_FILE_RDWR, &gfp)) != 0){
						printf("old open :%d\n", gerr);
					}
					//header
					if((gerr = gfs_pio_read(gfp, &header_nprocs,  sizeof(int), &g_np)) != 0){
						printf("old header_nprocs :%d\n", gerr);
					}
					
							
					//fread(&header_nprocs, sizeof(int), 1, header);
					count_arr = malloc(sizeof(int)*(header_nprocs));
					if((gerr = gfs_pio_read(gfp, count_arr+myrank,  sizeof(int), &g_np)) != 0){
						printf("old count_arr :%d\n", gerr);
					}
					//fread(count_arr, sizeof(int), header_nprocs, header);
					
					//header
					flat_file_blocklens = (ADIO_Offset**)malloc(sizeof(ADIO_Offset*)*header_nprocs);
					flat_file_indices = (ADIO_Offset**)malloc(sizeof(ADIO_Offset*)*header_nprocs);
					flat_file_disp = (ADIO_Offset*)malloc(sizeof(ADIO_Offset)*header_nprocs);
					filetype_size_arr = malloc(sizeof(unsigned)*header_nprocs);
					filetype_extent_arr = malloc(sizeof(MPI_Aint)*header_nprocs);
					
					for(i=0;i<header_nprocs;i++){
						flat_file_blocklens[i] = (ADIO_Offset*)malloc(sizeof(ADIO_Offset)*count_arr[i]);
						flat_file_indices[i] =(ADIO_Offset*) malloc(sizeof(ADIO_Offset)*count_arr[i]);
					}
					
					if((gerr = gfs_pio_read(gfp, flat_file_disp+myrank,  sizeof(ADIO_Offset), &g_np)) != 0){
						printf("old flat_file_disp : %d\n", gerr);
					}
					if((gerr = gfs_pio_read(gfp, filetype_size_arr+myrank,  sizeof(unsigned), &g_np)) != 0){
						printf("old filetype_size_arr : %d\n", gerr);
					}
					if((gerr = gfs_pio_read(gfp, filetype_extent_arr+myrank,  sizeof(MPI_Aint), &g_np)) != 0){
						printf("old filetype_extent_arr : %d\n", gerr);
					}
					if((gerr = gfs_pio_read(gfp, flat_file_blocklens[myrank],  sizeof(ADIO_Offset)*count_arr[myrank], &g_np)) != 0){
						printf("old flat_file_blocklens : %d\n", gerr);
					}
					if((gerr = gfs_pio_read(gfp, flat_file_indices[myrank],  sizeof(ADIO_Offset)*count_arr[myrank], &g_np)) != 0){
						printf("old flat_file_indices : %d\n", gerr);
					}
					
					gfs_pio_close(gfp);
					
					//
					MPI_Allgather(count_arr+myrank, 1, MPI_INT, count_arr, 1, MPI_INT, fd->comm);
					MPI_Allgather(flat_file_disp+myrank, 1, ADIO_OFFSET, flat_file_disp, 1, ADIO_OFFSET, fd->comm);
					MPI_Allgather(filetype_size_arr+myrank, 1, MPI_UNSIGNED, filetype_size_arr, 1, MPI_UNSIGNED, fd->comm);
					MPI_Allgather(filetype_extent_arr+myrank, 1, MPI_AINT, filetype_extent_arr, 1, MPI_AINT, fd->comm);
					//count_arr_sum = 0;
					//for(i=0;i<nprocs;i++) count_arr_sum += count_arr[i];
					//tmp_blocklens = malloc(sizeof(ADIO_Offset)*count_arr_sum);
					for(i=0;i<nprocs;i++){
						MPI_Bcast(flat_file_blocklens[i], count_arr[i], ADIO_OFFSET, i, fd->comm);
						MPI_Bcast(flat_file_indices[i], count_arr[i], ADIO_OFFSET, i, fd->comm);
					}
					
					
					if(header_nprocs > nprocs){
						for(i=0;i<header_nprocs/nprocs;i++){
							if((header_nprocs-nprocs) > myrank){
								sprintf(gfarm_header_file, "%s/%d", tmp_metadirname, nprocs*(i+1)+myrank);
								if((gerr = gfs_pio_open(gfarm_header_file, GFARM_FILE_RDWR, &gfp)) != 0){
									printf("%d\n", gerr);
								}
								if((gerr = gfs_pio_seek(gfp, sizeof(int), 0, &result)) != 0){
									printf("%d\n", gerr);
								}
								if((gerr = gfs_pio_read(gfp, count_arr+(nprocs*(i+1)+myrank),  sizeof(int), &g_np)) != 0){
									printf("%d\n", gerr);
								}
								if((gerr = gfs_pio_read(gfp, flat_file_disp+(nprocs*(i+1)+myrank),  sizeof(ADIO_Offset), &g_np)) != 0){
									printf("%d\n", gerr);
								}
								if((gerr = gfs_pio_read(gfp, filetype_size_arr+(nprocs*(i+1)+myrank),  sizeof(unsigned), &g_np)) != 0){
									printf("%d\n", gerr);
								}
								if((gerr = gfs_pio_read(gfp, filetype_extent_arr+(nprocs*(i+1)+myrank),  sizeof(MPI_Aint), &g_np)) != 0){
									printf("%d\n", gerr);
								}
								if((gerr = gfs_pio_read(gfp, flat_file_blocklens[(nprocs*(i+1)+myrank)],  sizeof(ADIO_Offset)*count_arr[(nprocs*(i+1)+myrank)], &g_np)) != 0){
									printf("%d\n", gerr);
								}
								if((gerr = gfs_pio_read(gfp, flat_file_indices[(nprocs*(i+1)+myrank)],  sizeof(ADIO_Offset)*count_arr[(nprocs*(i+1)+myrank)], &g_np)) != 0){
									printf("%d\n", gerr);
								}
								gfs_pio_close(gfp);
							}
						}
					}
					//fclose(header);
					
					
					ghi->nprocs = header_nprocs;
					ghi->count = count_arr;
					ghi->filetype_size = filetype_size_arr;
					ghi->filetype_extent = filetype_extent_arr;
					ghi->blocklens = flat_file_blocklens;
					ghi->indices = flat_file_indices;
					ghi->gfp_num = 0;
					ghi->gfp = malloc(sizeof(GFS_File)*header_nprocs);
					ghi->gfp_flag = malloc(sizeof(int)*header_nprocs);
					ghi->data_size = malloc(sizeof(unsigned long)*header_nprocs);
					ghi->access_time = 0.0;
					ghi->all_time = 0.0;
					for(i=0;i<header_nprocs;i++){
						ghi->gfp_flag[i] = 0;
						ghi->data_size[i] = 0;
					}
					
					flag = 0;
					
					
					for(j=0;j<header_nprocs;j++){
						for(i=0;i<count_arr[j];i++){
			#ifdef GFARM_DEBUG
							printf("[%d/%d]  j:i=%d:%d  indices:blocklens=%d:%d\n", myrank, nprocs, j, i, flat_file_indices[j][i], flat_file_blocklens[j][i]);
			#endif

							if(flat_file->blocklens[0] == 0){
								if(flat_file->indices[1] >= filetype_extent_arr[j]){
									tmp_indices = flat_file->indices[1] % filetype_extent_arr[j];
								}else{
									tmp_indices = flat_file->indices[1];
								}
								if(tmp_indices >= flat_file_indices[j][i] 
										&& (tmp_indices < flat_file_indices[j][i] + flat_file_blocklens[j][i]) ){
									flag = 1;
									break;
								}
							}else{
								if(flat_file->indices[0] >= filetype_extent_arr[j]){
									tmp_indices = flat_file->indices[0] % filetype_extent_arr[j];
								}else{
									tmp_indices = flat_file->indices[0];
								}
								if(tmp_indices >= flat_file_indices[j][i] 
										&& (tmp_indices < flat_file_indices[j][i] + flat_file_blocklens[j][i]) ){
									flag = 1;
									break;
								}
							}
						}
						if(flag == 1) break;
					}
					
					if(flag != 0){
						//create file name
						sprintf(gfarm_filename, "%s/%d", tmp_dirname,  j);
						
						//open file
						if((gerr = gfs_pio_open(gfarm_filename, GFARM_FILE_RDWR, &gfp)) != 0){
							printf("%d\n", gerr);
						}
						ghi->filename = gfarm_filename;
						ghi->gfp[j] = gfp;
						ghi->gfp_flag[j] = 1;
						ghi->gfp_num = j;
						fd->fs_ptr = ghi;
						ghi->newfile = 0;
						ghi->is_collective = 0;
						ghi->gci = NULL;
						
			#ifdef GFARM_DEBUG
						printf("[%d/%d]    ADIO_Set_view:%s\n", myrank, nprocs, gfarm_filename);
						printf("[%d/%d]    open fd=%d\n", myrank, nprocs, ghi->gfp[j]);
			#endif
					}else{
			#ifdef GFARM_DEBUG
						printf("[%d/%d]    flag=0\n", myrank, nprocs);
			#endif
					}
			}
	}else{
			printf("[%d/%d] gfs_opendir. dir_name = %s. gerr = %d.\n", myrank, nprocs, fd->filename, gerr);
	}
					ghi->gfarm_view_flag = 1;
}

int ad_gfarm_check_view(ADIO_File fd, MPI_Aint *filetype_extent, unsigned *filetype_size)
{
	int i, myrank, nprocs;
	int *count_arr, overwrap;
	ADIO_Offset *disps;
	ADIO_Offset **filetype_blocklens, **filetype_indices;
	MPI_Aint *filetype_extent_arr;
	unsigned *filetype_size_arr;
	ADIOI_Flatlist_node *flat_file;

	ADIOI_GFARM_headerInf *ghi;
	ghi = (ADIOI_GFARM_headerInf*)fd->fs_ptr;
	//ghi = (ADIOI_GFARM_headerInf *)malloc(sizeof(ADIOI_GFARM_headerInf));

	flat_file = ADIOI_Flatlist;
	while(flat_file->type != fd->filetype) flat_file = flat_file->next;

	MPI_Comm_rank(fd->comm, &myrank);
	MPI_Comm_size(fd->comm, &nprocs);

	//init
	disps = (ADIO_Offset*)malloc(sizeof(ADIO_Offset)*nprocs);
	count_arr = (int*)malloc(sizeof(int)*nprocs);
	filetype_extent_arr = (MPI_Aint*)malloc(sizeof(MPI_Aint)*nprocs);
	filetype_size_arr = (unsigned*)malloc(sizeof(unsigned)*nprocs);
	filetype_blocklens = (ADIO_Offset**)malloc(sizeof(ADIO_Offset*)*nprocs);
	filetype_indices = (ADIO_Offset**)malloc(sizeof(ADIO_Offset*)*nprocs);
	for(i=0;i<nprocs;i++){
			filetype_blocklens[i] = (ADIO_Offset*)malloc(sizeof(ADIO_Offset)*(flat_file->count));
			filetype_indices[i] = (ADIO_Offset*)malloc(sizeof(ADIO_Offset)*(flat_file->count));
	}

	//exchange view
/*	MPI_Allgather(&(flat_file->count),1,MPI_INT,count_arr,1,MPI_INT,fd->comm); 
	MPI_Allgather(filetype_extent,1,MPI_AINT,filetype_extent_arr,1,MPI_AINT,fd->comm);
	MPI_Allgather(filetype_size,1,MPI_UNSIGNED,filetype_size_arr,1,MPI_UNSIGNED,fd->comm);
	filetype_blocklens[myrank] = flat_file->blocklens;
	filetype_indices[myrank] = flat_file->indices;
	for(i=0;i<nprocs;i++){
		MPI_Bcast(filetype_blocklens[i], count_arr[i], ADIO_OFFSET, i, fd->comm);
		MPI_Bcast(filetype_indices[i], count_arr[i], ADIO_OFFSET, i, fd->comm);
	}
*/
	//check view
	overwrap = 0;

	//store
	ghi->nprocs = nprocs;
	ghi->count = count_arr;
	ghi->filetype_size = filetype_size_arr;
	ghi->filetype_extent = filetype_extent_arr;
	ghi->blocklens = filetype_blocklens;
	ghi->indices = filetype_indices;

	return overwrap;
}


