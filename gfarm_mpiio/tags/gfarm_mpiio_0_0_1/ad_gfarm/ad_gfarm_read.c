/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 2001 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_gfarm.h"
#include "adioi.h"
#include "adio_extern.h"

void ADIOI_GFARM_ReadContig(ADIO_File fd, void *buf, int count, 
			     MPI_Datatype datatype, int file_ptr_type,
			     ADIO_Offset offset, ADIO_Status *status, int
			     *error_code)
{
    int myrank, nprocs, datatype_size, err=1;
    ADIO_Offset len;
    *error_code = MPI_SUCCESS;
    
    gfarm_off_t result;
    int g_np;
    ADIOI_GFARM_headerInf *ghi;

    double st_time, fn_time;
    
    static char myname[] = "ADIOI_GFARM_READCONTIG";

    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);
    MPI_Type_size(datatype, &datatype_size);

#ifdef GFARM_DEBUG_READ
    FPRINTF(stdout, "[%d/%d] ADIOI_GFARM_ReadContig called on %s\n", myrank, 
	    nprocs, fd->filename);
#endif
	    
    MPI_Type_size(datatype, &datatype_size);
    len = (ADIO_Offset)datatype_size * (ADIO_Offset)count;
    ADIOI_Assert(len == (unsigned int) len); /* read takes an unsigned int parm */
    
    if (file_ptr_type == ADIO_INDIVIDUAL) {
	offset = fd->fp_ind;
    }


   
	ghi = (ADIOI_GFARM_headerInf *)(fd->fs_ptr);
	if(ghi->gfarm_view_flag != 1 && ghi->gfarm_view_flag != 2){
		if (fd->fp_sys_posn != offset) {
			err = lseek(fd->fd_sys, offset, SEEK_SET);
			/* --BEGIN ERROR HANDLING-- */
			if (err == -1) {
			    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
							       MPIR_ERR_RECOVERABLE,
							       myname, __LINE__,
							       MPI_ERR_IO, "**io",
							       "**io %s", strerror(errno));
			    fd->fp_sys_posn = -1;
			    return;
			}
			
		}
	}else{
		gfs_pio_seek(ghi->gfp[ghi->gfp_index], (gfarm_off_t)offset, GFARM_SEEK_SET, &result);
#ifdef GFARM_DEBUG_READ
		printf("[%d/%d] gfs_pio_seek:gfp=%d\n", myrank, nprocs, ghi->gfp[ghi->gfp_index]);
		printf("[%d/%d] read_contig seek result=%d\n", myrank, nprocs,result);
#endif
	}
	
	/* --END ERROR HANDLING-- */
    
    if(ghi->gfarm_view_flag != 1 && ghi->gfarm_view_flag != 2){
    	err = read(fd->fd_sys, buf, (unsigned int)len);
    }else{
    	st_time = MPI_Wtime();
    	err = gfs_pio_read(ghi->gfp[ghi->gfp_index], buf, (int)len, &g_np);
    	fn_time = MPI_Wtime();
    	ghi->data_size[ghi->gfp_index] += g_np;
    	ghi->access_time += fn_time -st_time;
#ifdef GFARM_DEBUG_READ
    	printf("[%d/%d] read_contig read err=%d, g_np=%d\n", myrank, nprocs,err,g_np);
    	printf("[%d/%d]    read len=%d\n", myrank, nprocs, len);
#endif

    }
    
    fd->fp_sys_posn = offset + g_np;

    if (file_ptr_type == ADIO_INDIVIDUAL) {
	fd->fp_ind += g_np; 
    }
    
    if(err == -1){
    	printf("read error!\n");
    }
/*
    if (file_ptr_type != ADIO_EXPLICIT_OFFSET)
    {
	offset = fd->fp_ind;
	fd->fp_ind += datatype_size * count;
	fd->fp_sys_posn = fd->fp_ind;
#if 0
	FPRINTF(stdout, "[%d/%d]    new file position is %lld\n", myrank, 
		nprocs, (long long) fd->fp_ind);
#endif
    }
    else {
	fd->fp_sys_posn = offset + datatype_size * count;
    }
*/
#ifdef GFARM_DEBUG_READ
    FPRINTF(stdout, "[%d/%d]    err=%ld\n",myrank, nprocs, err);
    FPRINTF(stdout, "[%d/%d]    reading (buf = %p, loc = %lld, sz = %lld)\n",
	    myrank, nprocs, buf, (long long) offset, 
	    (long long) datatype_size * count);
#endif

#ifdef HAVE_STATUS_SET_BYTES
    MPIR_Status_set_bytes(status, datatype, datatype_size * count);
#endif
}

void ADIOI_GFARM_ReadStrided(ADIO_File fd, void *buf, int count,
			      MPI_Datatype datatype, int file_ptr_type,
			      ADIO_Offset offset, ADIO_Status *status, int
			      *error_code)
{
    int myrank, nprocs;
    ADIOI_GFARM_headerInf *ghi;
    double st_time, fn_time;

    *error_code = MPI_SUCCESS;

    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);

#ifdef GFARM_DEBUG_READ
    FPRINTF(stdout, "[%d/%d] ADIOI_GFARM_ReadStrided called on %s\n", myrank, 
	    nprocs, fd->filename);
    FPRINTF(stdout, "[%d/%d]    calling ADIOI_GEN_ReadStrided\n", myrank, 
	    nprocs);
#endif

    ghi = (ADIOI_GFARM_headerInf *)(fd->fs_ptr);
    st_time = MPI_Wtime();

    ADIOI_GFARM_ReadStrided_naive(fd, buf, count, datatype, file_ptr_type, offset,
			  status, error_code);

    fn_time = MPI_Wtime();
    ghi->all_time += (fn_time - st_time);
}


void ADIOI_GFARM_ReadStrided_naive(ADIO_File fd, void *buf, int count,
                       MPI_Datatype buftype, int file_ptr_type,
                       ADIO_Offset offset, ADIO_Status *status, int
                       *error_code)
{
    /* offset is in units of etype relative to the filetype. */

    ADIOI_Flatlist_node *flat_buf, *flat_file;
    ADIO_Offset size, brd_size, frd_size=0, req_len, sum;
    int b_index;
    int n_etypes_in_filetype;
    ADIO_Offset n_filetypes, etype_in_filetype;
    ADIO_Offset abs_off_in_filetype=0;
    unsigned bufsize, filetype_size, buftype_size, size_in_filetype;
    int etype_size;
    MPI_Aint filetype_extent, buftype_extent; 
    int buf_count, buftype_is_contig, filetype_is_contig;
    ADIO_Offset userbuf_off;
    ADIO_Offset off, req_off, disp, end_offset=0, start_off;
    ADIO_Status status1;

    *error_code = MPI_SUCCESS;  /* changed below if error */

    ADIOI_Datatype_iscontig(buftype, &buftype_is_contig);
    ADIOI_Datatype_iscontig(fd->filetype, &filetype_is_contig);

    MPI_Type_size(fd->filetype, (int*)&filetype_size);
    if ( ! filetype_size ) {
	*error_code = MPI_SUCCESS; 
	return;
    }

    MPI_Type_extent(fd->filetype, &filetype_extent);
    MPI_Type_size(buftype,(int*) &buftype_size);
    MPI_Type_extent(buftype, &buftype_extent);
    etype_size = fd->etype_size;

    ADIOI_Assert((buftype_size * count) == ((ADIO_Offset)buftype_size * (ADIO_Offset)count));
    bufsize = buftype_size * count;

    /* contiguous in buftype and filetype is handled elsewhere */

    if (!buftype_is_contig && filetype_is_contig) {

    }else {  /* noncontiguous in file */
    	int f_index, st_index = 0; 
      ADIO_Offset st_n_filetypes;
      ADIO_Offset st_frd_size;
	int flag;

        /* First we're going to calculate a set of values for use in all
	 * the noncontiguous in file cases:
	 * start_off - starting byte position of data in file
	 * end_offset - last byte offset to be acessed in the file
	 * st_n_filetypes - how far into the file we start in terms of
	 *                  whole filetypes
	 * st_index - index of block in first filetype that we will be
	 *            starting in (?)
	 * st_frd_size - size of the data in the first filetype block
	 *               that we will read (accounts for being part-way
	 *               into reading this block of the filetype
	 *
	 */

	/* filetype already flattened in ADIO_Open */
	flat_file = ADIOI_Flatlist;
	while (flat_file->type != fd->filetype) flat_file = flat_file->next;
	disp = fd->disp;

	if (file_ptr_type == ADIO_INDIVIDUAL) {
	    start_off = fd->fp_ind; /* in bytes */
	    n_filetypes = -1;
	    flag = 0;
	    while (!flag) {
                n_filetypes++;
		for (f_index=0; f_index < flat_file->count; f_index++) {
		    if (disp + flat_file->indices[f_index] + 
                       n_filetypes*(ADIO_Offset)filetype_extent + 
		       flat_file->blocklens[f_index] >= start_off) 
		    {
		    	/* this block contains our starting position */

			st_index = f_index;
			frd_size = disp + flat_file->indices[f_index] + 
		 	           n_filetypes*(ADIO_Offset)filetype_extent + 
				   flat_file->blocklens[f_index] - start_off;
			flag = 1;
			break;
		    }
		}
	    }
	}
	else {
	    n_etypes_in_filetype = filetype_size/etype_size;
	    n_filetypes = offset / n_etypes_in_filetype;
	    etype_in_filetype = (int) (offset % n_etypes_in_filetype);
	    size_in_filetype = (unsigned)etype_in_filetype * (unsigned)etype_size;
 
	    sum = 0;
	    for (f_index=0; f_index < flat_file->count; f_index++) {
		sum += flat_file->blocklens[f_index];
		if (sum > size_in_filetype) {
		    st_index = f_index;
		    frd_size = sum - size_in_filetype;
		    abs_off_in_filetype = flat_file->indices[f_index] +
			                  size_in_filetype - 
			                  (sum - flat_file->blocklens[f_index]);
		    break;
		}
	    }

	    /* abs. offset in bytes in the file */
	    start_off = disp + n_filetypes*(ADIO_Offset)filetype_extent + 
	    	        abs_off_in_filetype;
	}

	st_frd_size = frd_size;
	st_n_filetypes = n_filetypes;

	/* start_off, st_n_filetypes, st_index, and st_frd_size are 
	 * all calculated at this point
	 */

        /* Calculate end_offset, the last byte-offset that will be accessed.
         * e.g., if start_off=0 and 100 bytes to be read, end_offset=99
	 */
	userbuf_off = 0;
	f_index = st_index;
	off = start_off;
	frd_size = ADIOI_MIN(st_frd_size, bufsize);
	while (userbuf_off < bufsize) {
	    userbuf_off += frd_size;
	    end_offset = off + frd_size - 1;

	    if (f_index < (flat_file->count - 1)) f_index++;
	    else {
		f_index = 0;
		n_filetypes++;
	    }

	    off = disp + flat_file->indices[f_index] + 
	          n_filetypes*(ADIO_Offset)filetype_extent;
	    frd_size = ADIOI_MIN(flat_file->blocklens[f_index], 
	                         bufsize-(unsigned)userbuf_off);
	}

	/* End of calculations.  At this point the following values have
	 * been calculated and are ready for use:
	 * - start_off
	 * - end_offset
	 * - st_n_filetypes
	 * - st_index
	 * - st_frd_size
	 */

	/* if atomicity is true, lock (exclusive) the region to be accessed */
        if ((fd->atomicity) && ADIO_Feature(fd, ADIO_LOCKS))
	{
            ADIOI_WRITE_LOCK(fd, start_off, SEEK_SET, end_offset-start_off+1);
	}

	if (buftype_is_contig && !filetype_is_contig) {
	    /* contiguous in memory, noncontiguous in file. should be the
	     * most common case.
	     */

	    userbuf_off = 0;
	    f_index = st_index;
	    off = start_off;
	    n_filetypes = st_n_filetypes;
	    frd_size = ADIOI_MIN(st_frd_size, bufsize);

	    /* while there is still space in the buffer, read more data */
	    while (userbuf_off < bufsize) {
                if (frd_size) { 
                    /* TYPE_UB and TYPE_LB can result in 
                       frd_size = 0. save system call in such cases */ 
		    req_off = off;
		    req_len = frd_size;

        ADIOI_Assert((((ADIO_Offset)(MPIR_Upint)buf) + userbuf_off) == (ADIO_Offset)(MPIR_Upint)((MPIR_Upint)buf + userbuf_off));
        ADIOI_Assert(req_len == (int) req_len);
		    ADIOI_GFARM_read_gfarm(fd, 
				    (char *) buf + userbuf_off,
				    req_len, 
				    MPI_BYTE, 
				    ADIO_EXPLICIT_OFFSET,
				    req_off,
				    &status1,
				    -1,
				    error_code);
		    if (*error_code != MPI_SUCCESS) return;
		}
		userbuf_off += frd_size;

                if (off + frd_size < disp + flat_file->indices[f_index] +
                   flat_file->blocklens[f_index] + 
		   n_filetypes*(ADIO_Offset)filetype_extent)
		{
		    /* important that this value be correct, as it is
		     * used to set the offset in the fd near the end of
		     * this function.
		     */
                    off += frd_size;
		}
                /* did not reach end of contiguous block in filetype.
                 * no more I/O needed. off is incremented by frd_size.
		 */
                else {
		    if (f_index < (flat_file->count - 1)) f_index++;
		    else {
			f_index = 0;
			n_filetypes++;
		    }
		    off = disp + flat_file->indices[f_index] + 
                          n_filetypes*(ADIO_Offset)filetype_extent;
		    frd_size = ADIOI_MIN(flat_file->blocklens[f_index], 
		                         bufsize-(unsigned)userbuf_off);
		}
	    }
	}
	else {
	    ADIO_Offset i_offset, tmp_bufsize = 0;
	    /* noncontiguous in memory as well as in file */

	    ADIOI_Flatten_datatype(buftype);
	    flat_buf = ADIOI_Flatlist;
	    while (flat_buf->type != buftype) flat_buf = flat_buf->next;

	    b_index = buf_count = 0;
	    i_offset = flat_buf->indices[0];
	    f_index = st_index;
	    off = start_off;
	    n_filetypes = st_n_filetypes;
	    frd_size = st_frd_size;
	    brd_size = flat_buf->blocklens[0];

	    /* while we haven't read size * count bytes, keep going */
	    while (tmp_bufsize < bufsize) {
    		ADIO_Offset new_brd_size = brd_size, new_frd_size = frd_size;

		size = ADIOI_MIN(frd_size, brd_size);
		if (size) {
		    req_off = off;
		    req_len = size;
		    userbuf_off = i_offset;

        ADIOI_Assert((((ADIO_Offset)(MPIR_Upint)buf) + userbuf_off) == (ADIO_Offset)(MPIR_Upint)((MPIR_Upint)buf + userbuf_off));
        ADIOI_Assert(req_len == (int) req_len);
		    ADIOI_GFARM_read_gfarm(fd, 
				    (char *) buf + userbuf_off,
				    req_len, 
				    MPI_BYTE, 
				    ADIO_EXPLICIT_OFFSET,
				    req_off,
				    &status1,
				    -1,
				    error_code);
		    if (*error_code != MPI_SUCCESS) return;
		}

		if (size == frd_size) {
		    /* reached end of contiguous block in file */
		    if (f_index < (flat_file->count - 1)) f_index++;
		    else {
			f_index = 0;
			n_filetypes++;
		    }

		    off = disp + flat_file->indices[f_index] + 
                          n_filetypes*(ADIO_Offset)filetype_extent;

		    new_frd_size = flat_file->blocklens[f_index];
		    if (size != brd_size) {
			i_offset += size;
			new_brd_size -= size;
		    }
		}

		if (size == brd_size) {
		    /* reached end of contiguous block in memory */

		    b_index = (b_index + 1)%flat_buf->count;
		    buf_count++;
		    i_offset = buftype_extent*(buf_count/flat_buf->count) +
			flat_buf->indices[b_index];
		    new_brd_size = flat_buf->blocklens[b_index];
		    if (size != frd_size) {
			off += size;
			new_frd_size -= size;
		    }
		}
		tmp_bufsize += size;
		frd_size = new_frd_size;
                brd_size = new_brd_size;
	    }
	}

	/* unlock the file region if we locked it */
        if ((fd->atomicity) && (fd->file_system != ADIO_PIOFS) && 
	   (fd->file_system != ADIO_PVFS) && (fd->file_system != ADIO_PVFS2))
	{
            ADIOI_UNLOCK(fd, start_off, SEEK_SET, end_offset-start_off+1);
	}

	if (file_ptr_type == ADIO_INDIVIDUAL) fd->fp_ind = off;
    } /* end of (else noncontiguous in file) */

    fd->fp_sys_posn = -1;   /* mark it as invalid. */

#ifdef HAVE_STATUS_SET_BYTES
    MPIR_Status_set_bytes(status, buftype, bufsize);
    /* This is a temporary way of filling in status. The right way is to 
     * keep track of how much data was actually read and placed in buf 
     */
#endif

    if (!buftype_is_contig) ADIOI_Delete_flattened(buftype);
}


/*---------------------------------------*/
//
//
//
//
//
/*---------------------------------------*/
void ADIOI_GFARM_read_gfarm(ADIO_File fd, void *buf, int count, 
			MPI_Datatype datatype, int file_ptr_type,
			ADIO_Offset offset, ADIO_Status *status,
			int index, int *error_code)
{
	ADIOI_Flatlist_node *flat_file;
	ADIO_Offset blocklen_sum, gfarm_offset;
	unsigned filetype_size;
	MPI_Aint filetype_extent;
	int i, j, nprocs, myrank;
	int filetype_count;
	ADIO_Offset **blocklens, **indices;
	ADIO_Offset tmp_offset;
	char tmpfilename[256];
	void *tmp_buf;
	
	int gfarm_count;
	ADIOI_GFARM_headerInf *ghi;
	GFS_File gfp;
	gfarm_error_t gerr;
	
	ghi = (ADIOI_GFARM_headerInf *)fd->fs_ptr;
	tmp_buf = buf;
	MPI_Comm_size(fd->comm, &nprocs);
	MPI_Comm_rank(fd->comm, &myrank);
	
	if(index == -1){
		index = ghi->gfp_num;
	}else{
		if(index >= ghi->nprocs){
			index = 0;
		}
	}
	
	
	if(ghi->gfarm_view_flag == 2){
		ADIOI_GFARM_ReadContig(fd, buf, count, datatype, file_ptr_type, offset, status, error_code);
		return;
	}
	
/*	
	printf("[%d/%d] blocklen\n", myrank, nprocs);
	for(i=0;i<Gfarm_headerlist->count[myrank];i++){
		printf("%d, ", Gfarm_headerlist->blocklens[myrank][i]);
	}
	printf("\n[%d/%d] indices\n", myrank, nprocs, count);
	for(i=0;i<Gfarm_headerlist->count[myrank];i++){
		printf("%d, ", Gfarm_headerlist->indices[myrank][i]);
	}
	printf("\n");
*/	
	
	filetype_count = offset/(ghi->filetype_extent[index]);
	
	tmp_offset = offset - (filetype_count * (ghi->filetype_extent[index]));
	
//printf("[%d/%d] filetype extent = %d \n", myrank, nprocs, Gfarm_headerlist->filetype_extent);
//printf("[%d/%d] buf size = %d \n", myrank, nprocs, count);

	for(i=0;i<ghi->count[index];i++){
#ifdef GFARM_DEBUG_READ
	printf("[%d/%d] check offset.\n", myrank, nprocs);
	printf("[%d/%d] %d %d, %d, %d\n", myrank, nprocs, index, ghi->blocklens[index][i], ghi->indices[index][i], tmp_offset);
#endif
		if(ghi->blocklens[index][i] == 0) continue;
		//自分のローカルファイルにあるかどうか
		if(ghi->indices[index][i] <= tmp_offset && ghi->blocklens[index][i] + ghi->indices[index][i] > tmp_offset){
			//他のファイルとまたぐ場合
			if(tmp_offset+count > ghi->indices[index][i] + ghi->blocklens[index][i]){
				//printf("[%d/%d] 他のファイルとまたぐ offset = %d \n", myrank, nprocs, offset);
				
				if(ghi->gfp_flag[index] == 0){
					//sprintf(tmpfilename, "./%s_g/%d", ghi->filename, index);
					sprintf(tmpfilename, "./%s_g/%d", fd->filename, index);
#ifdef GFARM_DEBUG_READ
					printf("[%d/%d] new file open.name = %s\n", myrank, nprocs, tmpfilename);
#endif
					if((gerr = gfs_pio_open(tmpfilename, GFARM_FILE_RDWR, &gfp)) != 0){
						printf("%d\n", gerr);
					}
					ghi->gfp[index] = gfp;
					ghi->gfp_flag[index] = 1;
					ghi->gfp_index = index;
				}else{
					ghi->gfp_index = index;
				}
				
				blocklen_sum = 0;
				for(j=0;j<i;j++){
					blocklen_sum += ghi->blocklens[index][j];
				}
				
				gfarm_offset = ghi->filetype_size[index] * filetype_count + (blocklen_sum + tmp_offset - ghi->indices[index][i]);
				gfarm_count = count - (tmp_offset + count -(ghi->indices[index][i] + ghi->blocklens[index][i]));
				
#ifdef GFARM_DEBUG_READ
	printf("[%d/%d] local and remote.\n", myrank, nprocs);
	printf("[%d/%d] %d, %d, %d, %d, %d\n", myrank, nprocs, ghi->filetype_size[index], filetype_count, blocklen_sum, tmp_offset, ghi->indices[index][i]);
	printf("[%d/%d] offset -> gfarm_offset = %d ->%d \n", myrank, nprocs, offset, gfarm_offset);
#endif
				
				ADIOI_GFARM_ReadContig(fd, tmp_buf, gfarm_count, datatype, file_ptr_type, gfarm_offset, status, error_code);
				//
				ADIOI_GFARM_read_gfarm(fd, tmp_buf+gfarm_count, (count-gfarm_count), datatype, file_ptr_type, offset+gfarm_count, status, (index+1), error_code);
				return;
			//ローカルファイルのみでいい場合
			}else{
				
				if(ghi->gfp_flag[index] == 0){
					sprintf(tmpfilename, "./%s_g/%d", fd->filename, index);
#ifdef GFARM_DEBUG_READ
					printf("[%d/%d] new file open.name = %s\n", myrank, nprocs, tmpfilename);
#endif
					if((gerr = gfs_pio_open(tmpfilename, GFARM_FILE_RDWR, &gfp)) != 0){
						printf("%d\n", gerr);
					}
					ghi->gfp[index] = gfp;
					ghi->gfp_flag[index] = 1;
					ghi->gfp_index = index;
				}else{
					ghi->gfp_index = index;
				}
				
				blocklen_sum = 0;
				for(j=0;j<i;j++){
					blocklen_sum += ghi->blocklens[index][j];
				}
				gfarm_offset = ghi->filetype_size[index] * filetype_count + (blocklen_sum + tmp_offset - ghi->indices[index][i]);
#ifdef GFARM_DEBUG_READ
	printf("[%d/%d] local only.\n", myrank, nprocs);
	printf("[%d/%d] offset -> gfarm_offset = %d ->%d \n", myrank, nprocs, offset, gfarm_offset);
#endif
				ADIOI_GFARM_ReadContig(fd, tmp_buf, count, datatype, file_ptr_type, gfarm_offset, status, error_code);
				return;
			}
		}
	}
	
	//自分のローカルファイルにない
#ifdef GFARM_DEBUG_READ
	printf("[%d/%d] not local.\n", myrank, nprocs);
	printf("[%d/%d] offset -> tmp_offset = %d -> %d\n", myrank, nprocs, offset, tmp_offset);
#endif
	ADIOI_GFARM_read_gfarm(fd, tmp_buf, count, datatype, file_ptr_type, offset, status, (index+1), error_code);
	return;
}
