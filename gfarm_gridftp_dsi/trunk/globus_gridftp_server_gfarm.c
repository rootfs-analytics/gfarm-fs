/*
 * Globus GridFTP Server DSI for Gfarm File System
 *
 * Copyright (c) 1999-2006 University of Chicago
 *
 * Copyright (c) 2009 University of Tsukuba.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "globus_gridftp_server.h"
#include <gfarm/gfarm.h>
#include <libgen.h>

static globus_version_t local_version =
{
	1, /* major version number */
	0, /* minor version number */
	1236013521,
	0 /* branch ID */
};

typedef struct globus_l_gfs_gfarm_handle_s
{
	int concurrency;
	globus_size_t block_size;
	globus_bool_t buffers_initialized;
	globus_byte_t **buffers;

	globus_mutex_t mutex;
	GFS_File gf;
	globus_off_t offset;
	globus_bool_t done;
	globus_bool_t eof;
	int concurrency_count;
	globus_off_t read_len;  /* not use */
	globus_result_t save_result;
} globus_l_gfs_gfarm_handle_t;

extern void gfarm_gsi_set_delegated_cred(gss_cred_id_t);
extern char *gfarm_gsi_client_cred_name(void);

#define DSI_BLOCKSIZE   "GFARM_DSI_BLOCKSIZE"
#define DSI_CONCURRENCY "GFARM_DSI_CONCURRENCY"

/*************************************************************************
 *  start
 *  -----
 *  This function is called when a new session is initialized, ie a user
 *  connectes to the server.  This hook gives the dsi an oppertunity to
 *  set internal state that will be threaded through to all other
 *  function calls associated with this session.  And an oppertunity to
 *  reject the user.
 *
 *  finished_info.info.session.session_arg should be set to an DSI
 *  defined data structure.  This pointer will be passed as the void *
 *  user_arg parameter to all other interface functions.
 * 
 *  NOTE: at nice wrapper function should exist that hides the details
 *        of the finished_info structure, but it currently does not.
 *        The DSI developer should jsut follow this template for now
 ************************************************************************/
static void
globus_l_gfs_gfarm_start(
	globus_gfs_operation_t op,
	globus_gfs_session_info_t *session_info)
{
	globus_l_gfs_gfarm_handle_t *gfarm_handle;
	globus_gfs_finished_info_t finished_info;
	gfarm_error_t e;
	GlobusGFSName(globus_l_gfs_gfarm_start);

	gfarm_handle = (globus_l_gfs_gfarm_handle_t *)
		globus_malloc(sizeof(globus_l_gfs_gfarm_handle_t));

	memset(&finished_info, '\0', sizeof(globus_gfs_finished_info_t));
	finished_info.type = GLOBUS_GFS_OP_SESSION_START;
	finished_info.info.session.session_arg = gfarm_handle;
	finished_info.info.session.username = session_info->username;
	finished_info.info.session.home_dir = "/";
	finished_info.result = GLOBUS_SUCCESS;

	gfarm_gsi_set_delegated_cred(session_info->del_cred);
	globus_gfs_log_message(
		GLOBUS_GFS_LOG_INFO,
		"[gfarm-dsi] gfarm_gsi_client_cred_name: %s\n",
		gfarm_gsi_client_cred_name());

	globus_mutex_init(&gfarm_handle->mutex, NULL);
	gfarm_handle->buffers_initialized = GLOBUS_FALSE;

	e = gfarm_initialize(NULL, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		finished_info.result =
			GlobusGFSErrorSystemError(
				"gfarm_initialize",
				gfarm_error_to_errno(e));
	}

	globus_gridftp_server_operation_finished(
		op, finished_info.result, &finished_info);
}

/*************************************************************************
 *  destroy
 *  -------
 *  This is called when a session ends, ie client quits or disconnects.
 *  The dsi should clean up all memory they associated wit the session
 *  here.
 ************************************************************************/
static void buffers_destroy(globus_l_gfs_gfarm_handle_t *gfarm_handle);

static void
globus_l_gfs_gfarm_destroy(void *user_arg)
{
	globus_l_gfs_gfarm_handle_t *gfarm_handle;

	gfarm_handle = (globus_l_gfs_gfarm_handle_t *) user_arg;

	globus_mutex_destroy(&gfarm_handle->mutex);
	buffers_destroy(gfarm_handle);

	globus_free(gfarm_handle);
	gfarm_terminate();
}

/*************************************************************************
 *  stat
 *  ----
 *  This interface function is called whenever the server needs
 *  information about a given file or resource.  It is called then an
 *  LIST is sent by the client, when the server needs to verify that
 *  a file exists and has the proper permissions, etc.
 ************************************************************************/
static uid_t
get_uid(char *user)
{
	if (strcmp(gfarm_get_global_username(), user) == 0)
		return getuid(); /* my own file */
	/* XXX FIXME - some other's file */
	return (0);
}

static int
get_gid(char *group)
{
	/* XXX FIXME */
	return (0);
}

static int
get_nlink(struct gfs_stat *st)
{
	/* XXX FIXME */
	return (GFARM_S_ISDIR(st->st_mode) ? 32000 : st->st_nlink);
}

static void
stat_array_copy(globus_gfs_stat_t *dst, struct gfs_stat *src)
{
	dst->ino = src->st_ino;
	dst->mode = src->st_mode;
	dst->nlink = get_nlink(src);
	dst->uid = get_uid(src->st_user);
	dst->gid = get_gid(src->st_group);
	dst->size = src->st_size;
	dst->atime = src->st_atimespec.tv_sec;
	dst->mtime = src->st_mtimespec.tv_sec;
	dst->ctime = src->st_ctimespec.tv_sec;
}

static void
stat_array_destroy(globus_gfs_stat_t *stat_array, int stat_count)
{
	int i;

	if (stat_array == NULL) {
		return;
	}
	for(i = 0; i < stat_count; i++)	{
		if(stat_array[i].name != NULL) {
			globus_free(stat_array[i].name);
		}
		if(stat_array[i].symlink_target != NULL) {
			globus_free(stat_array[i].symlink_target);
		}
	}
	globus_free(stat_array);
}

static gfarm_error_t
stat_array_set(
	globus_gfs_stat_t *stat_array,
	const char *path, const char *name)
{
	struct gfs_stat st;
	gfarm_error_t e;

	stat_array->symlink_target = NULL;
	e = gfs_stat(path, &st);
	if (e != GFARM_ERR_NO_ERROR) {
		stat_array->name = NULL;
		return (e);
	}
	stat_array_copy(&stat_array[0], &st);
	stat_array->name = strdup(name);
	gfs_stat_free(&st);
	return (GFARM_ERR_NO_ERROR);
}

static void
globus_l_gfs_gfarm_stat(
	globus_gfs_operation_t op,
	globus_gfs_stat_info_t *stat_info, void *user_arg)
{
	globus_gfs_stat_t  *stat_array = NULL;
	int stat_count = 0;
	globus_l_gfs_gfarm_handle_t *gfarm_handle;
	globus_result_t result;
	gfarm_error_t e;
	struct gfs_stat st;
	char *path;
	int is_dir;
	GFS_Dir dp;
	struct gfs_dirent *de;
	gfarm_error_t e2;
	int array_size = 4;
	GlobusGFSName(globus_l_gfs_gfarm_stat);

	gfarm_handle = (globus_l_gfs_gfarm_handle_t *) user_arg;
	path = stat_info->pathname;

	e = gfs_stat(path, &st); /* XXX replace gfs_lstat() */
	if (e != GFARM_ERR_NO_ERROR) {
		result = GlobusGFSErrorSystemError(
			"gfs_stat",
			gfarm_error_to_errno(e));
		goto error;
        }
#if 0
	if (GFARM_S_ISLNK(st.st_mode)) {
		/* XXX replace st from gfs_stat() */
	}
#endif
	is_dir = GFARM_S_ISDIR(st.st_mode);
	gfs_stat_free(&st);
	if (!is_dir || stat_info->file_only) { /* stat */
		stat_array = (globus_gfs_stat_t *)
			globus_malloc(sizeof(globus_gfs_stat_t));
		if(stat_array == NULL) {
			result = GlobusGFSErrorMemory("stat_array");
			goto error;
		}
		stat_count = 1;
		e = stat_array_set(&stat_array[0], path, basename(path));
		if (e != GFARM_ERR_NO_ERROR) {
			result = GlobusGFSErrorSystemError(
				"gfs_stat",
				gfarm_error_to_errno(e));
			goto error;
		}
		goto end; /* success */
	}
	/* list */
	e = gfs_opendir(path, &dp);
	if (e != GFARM_ERR_NO_ERROR) {
		result = GlobusGFSErrorSystemError(
			"gfs_opendir",
			gfarm_error_to_errno(e));
		goto error;
	}
	while ((e = gfs_readdir(dp, &de)) == GFARM_ERR_NO_ERROR &&
	       de != NULL) {
		char child[MAXPATHLEN];
		stat_count++;
		if (stat_array == NULL) {
			stat_array = (globus_gfs_stat_t *)
				globus_malloc(sizeof(globus_gfs_stat_t)
					      * array_size);
		} else if (stat_count > array_size) {
			array_size = array_size * 2;
			globus_gfs_stat_t *tmp =
				(globus_gfs_stat_t *)
				globus_realloc(stat_array,
					       sizeof(globus_gfs_stat_t)
					       * array_size);
			if (tmp == NULL) {
				globus_free(stat_array);
			}
			stat_array = tmp;
		}
		if(stat_array == NULL) {
			result = GlobusGFSErrorMemory("stat_array");
			gfs_closedir(dp);
			stat_count--;
			goto error;
		}
		snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
		child[MAXPATHLEN - 1] = '\0';
		e = stat_array_set(&stat_array[stat_count-1],
				   child, de->d_name);
		if (e != GFARM_ERR_NO_ERROR) {
			result = GlobusGFSErrorSystemError(
				"gfs_stat",
				gfarm_error_to_errno(e));
			goto error;
		}
	}
	e2 = gfs_closedir(dp);
	if (e == GFARM_ERR_NO_ERROR)
		e = e2;
	if (e != GFARM_ERR_NO_ERROR) {
		result = GlobusGFSErrorSystemError(
			"gfs_readdir",
			gfarm_error_to_errno(e));
		goto error;
	}
end:
	globus_gridftp_server_finished_stat(
		op, GLOBUS_SUCCESS, stat_array, stat_count);
	stat_array_destroy(stat_array, stat_count);
	return;
error:
	globus_gridftp_server_finished_stat(op, result, NULL, 0);
	stat_array_destroy(stat_array, stat_count);
	return;
}

/*************************************************************************
 *  command
 *  -------
 *  This interface function is called when the client sends a 'command'.
 *  commands are such things as mkdir, remdir, delete.  The complete
 *  enumeration is below.
 *
 *  To determine which command is being requested look at:
 *      cmd_info->command
 *
 *      GLOBUS_GFS_CMD_MKD = 1,
 *      GLOBUS_GFS_CMD_RMD,
 *      GLOBUS_GFS_CMD_DELE,
 *      GLOBUS_GFS_CMD_RNTO,
 *      GLOBUS_GFS_CMD_RNFR,
 *      GLOBUS_GFS_CMD_CKSM,
 *      GLOBUS_GFS_CMD_SITE_CHMOD,
 *      GLOBUS_GFS_CMD_SITE_DSI
 ************************************************************************/
static globus_result_t
gfarm_mkdir(globus_gfs_operation_t op, const char *pathname)
{
	gfarm_error_t e;
	mode_t um;
	GlobusGFSName(gfarm_mkdir);

	um = umask(0022);
	umask(um);
	e = gfs_mkdir(pathname, 0777 & ~um);
	if (e != GFARM_ERR_NO_ERROR) {
		return GlobusGFSErrorSystemError(
			"gfs_mkdir",
			gfarm_error_to_errno(e));
	}
	return (GLOBUS_SUCCESS);
}

static globus_result_t
gfarm_rmdir(globus_gfs_operation_t op, const char *pathname)
{
	gfarm_error_t e;
	GlobusGFSName(gfarm_rmdir);

	e = gfs_rmdir(pathname);
	if (e != GFARM_ERR_NO_ERROR) {
		return GlobusGFSErrorSystemError(
			"gfs_rmdir",
			gfarm_error_to_errno(e));
	}
	return (GLOBUS_SUCCESS);
}

static globus_result_t
gfarm_delete(globus_gfs_operation_t op, const char *pathname)
{
	gfarm_error_t e;
	GlobusGFSName(gfarm_delete);

	e = gfs_unlink(pathname);
	if (e != GFARM_ERR_NO_ERROR) {
		return GlobusGFSErrorSystemError(
			"gfs_unlink",
			gfarm_error_to_errno(e));
	}
	return (GLOBUS_SUCCESS);
}

static globus_result_t
gfarm_rename(globus_gfs_operation_t op, const char *from, const char *to)
{
	gfarm_error_t e;
	GlobusGFSName(gfarm_rename);

	e = gfs_rename(from, to);
	if (e != GFARM_ERR_NO_ERROR) {
		return GlobusGFSErrorSystemError(
			"gfs_rename",
			gfarm_error_to_errno(e));
	}
	return (GLOBUS_SUCCESS);
}

static globus_result_t
gfarm_chmod(globus_gfs_operation_t op, const char *pathname, mode_t mode)
{
	gfarm_error_t e;
	GlobusGFSName(gfarm_chmod);

	e = gfs_chmod(pathname, mode & 0777);
	if (e != GFARM_ERR_NO_ERROR) {
		return GlobusGFSErrorSystemError(
			"gfs_chmod",
			gfarm_error_to_errno(e));
	}
	return (GLOBUS_SUCCESS);
}

static void
globus_l_gfs_gfarm_command(
	globus_gfs_operation_t op,
	globus_gfs_command_info_t *cmd_info, void *user_arg)
{
	globus_l_gfs_gfarm_handle_t *gfarm_handle;
	globus_result_t result;
	GlobusGFSName(globus_l_gfs_gfarm_command);

	gfarm_handle = (globus_l_gfs_gfarm_handle_t *) user_arg;

	switch (cmd_info->command) {
	case GLOBUS_GFS_CMD_MKD:
		result = gfarm_mkdir(op, cmd_info->pathname);
		break;
	case GLOBUS_GFS_CMD_RMD:
		result = gfarm_rmdir(op, cmd_info->pathname);
		break;
	case GLOBUS_GFS_CMD_DELE:
		result = gfarm_delete(op, cmd_info->pathname);
		break;
	case GLOBUS_GFS_CMD_RNTO:
		result = gfarm_rename(
			op, cmd_info->rnfr_pathname, cmd_info->pathname);
		break;
	case GLOBUS_GFS_CMD_SITE_CHMOD:
		result = gfarm_chmod(
			op, cmd_info->pathname, cmd_info->chmod_mode);
		break;
	default:
		result = GLOBUS_FAILURE;
		break;
	}

	globus_gridftp_server_finished_command(op, result, GLOBUS_NULL);
}

/*************************************************************************
 *  recv
 *  ----
 *  This interface function is called when the client requests that a
 *  file be transfered to the server.
 *
 *  To receive a file the following functions will be used in roughly
 *  the presented order.  They are doced in more detail with the
 *  gridftp server documentation.
 *
 *      globus_gridftp_server_begin_transfer();
 *      globus_gridftp_server_register_read();
 *      globus_gridftp_server_finished_transfer();
 *
 ************************************************************************/
static void
buffers_destroy(globus_l_gfs_gfarm_handle_t *gfarm_handle)
{
	int i;

	if (!gfarm_handle->buffers_initialized)
		return;

	for (i = 0; i < gfarm_handle->concurrency; i++) {
		if (gfarm_handle->buffers[i] != NULL)
			globus_free(gfarm_handle->buffers[i]);
	}
	globus_free(gfarm_handle->buffers);
	gfarm_handle->buffers_initialized = GLOBUS_FALSE;
}

static globus_result_t
buffers_initialize(
	globus_gfs_operation_t op, globus_l_gfs_gfarm_handle_t *gfarm_handle)
{
	globus_result_t result;
	int i;
	char *envstr;
	GlobusGFSName(buffers_initialize);

	if (gfarm_handle->buffers_initialized)
		return (GLOBUS_SUCCESS);

	envstr = getenv(DSI_BLOCKSIZE);
	if (envstr != NULL)
		gfarm_handle->block_size = atoi(envstr);
	else
		gfarm_handle->block_size = 0;
	envstr = getenv(DSI_CONCURRENCY);
	if (envstr != NULL)
		gfarm_handle->concurrency = atoi(envstr);
	else
		gfarm_handle->concurrency = 0;
	if (gfarm_handle->block_size <= 0)
		globus_gridftp_server_get_block_size(
			op, &gfarm_handle->block_size);
	if (gfarm_handle->concurrency <= 0)
		globus_gridftp_server_get_optimal_concurrency(
			op, &gfarm_handle->concurrency);
	globus_gfs_log_message(
		GLOBUS_GFS_LOG_INFO,
		"[gfarm-dsi] %s=%d\n", DSI_BLOCKSIZE,
		gfarm_handle->block_size);
	globus_gfs_log_message(
		GLOBUS_GFS_LOG_INFO,
		"[gfarm-dsi] %s=%d\n", DSI_CONCURRENCY,
		gfarm_handle->concurrency);

	gfarm_handle->buffers = (globus_byte_t**) globus_calloc(
		gfarm_handle->concurrency, sizeof(globus_byte_t*));
	if (gfarm_handle->buffers == NULL) {
		result = GlobusGFSErrorMemory("buffers");
		goto error;
	}
	for (i = 0; i < gfarm_handle->concurrency; i++) {
		gfarm_handle->buffers[i] = (globus_byte_t*) globus_malloc(
			sizeof(globus_byte_t) * gfarm_handle->block_size);
		if (gfarm_handle->buffers[i] == NULL) {
			result = GlobusGFSErrorMemory("buffer");
			i++;
			for (; i < gfarm_handle->concurrency; i++) {
				gfarm_handle->buffers[i] = NULL;
			}
			gfarm_handle->buffers_initialized = GLOBUS_TRUE;
			goto error;
		}
	}
	gfarm_handle->buffers_initialized = GLOBUS_TRUE;
	return (GLOBUS_SUCCESS);
error:
	buffers_destroy(gfarm_handle);
	return (result);
}

static void
gfarm_import_cb(
	globus_gfs_operation_t op,
	globus_result_t result,
	globus_byte_t *buffer,
	globus_size_t nbytes,
	globus_off_t offset,
	globus_bool_t eof,
	void *user_arg)
{
	globus_l_gfs_gfarm_handle_t *gfarm_handle;
	globus_off_t o;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	int rv;
	GlobusGFSName(gfarm_read_cb);

	gfarm_handle = (globus_l_gfs_gfarm_handle_t *) user_arg;

	globus_mutex_lock(&gfarm_handle->mutex);
	if (gfarm_handle->done) {
		globus_mutex_unlock(&gfarm_handle->mutex);
		return;
	}
	if (result != GLOBUS_SUCCESS) {
		gfarm_handle->eof = GLOBUS_TRUE;
		goto finish;
	}
	if (nbytes == 0)
		goto skip;
	if (gfarm_handle->offset == 0 || gfarm_handle->offset != offset)
		e = gfs_pio_seek(gfarm_handle->gf, offset, GFARM_SEEK_SET, &o);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfs_pio_write(gfarm_handle->gf, buffer, nbytes, &rv);
	if (e != GFARM_ERR_NO_ERROR) {
		result = GlobusGFSErrorSystemError(
			"gfs_pio_write", gfarm_error_to_errno(e));
		gfarm_handle->eof = GLOBUS_TRUE;
                goto finish;
	}
	gfarm_handle->offset = offset + rv;
skip:
	if (eof) {
		result = GLOBUS_SUCCESS;
		gfarm_handle->eof = GLOBUS_TRUE;
		goto finish;
	}
	/* continue */
	globus_mutex_unlock(&gfarm_handle->mutex);
	globus_gridftp_server_register_read(
		op, buffer, gfarm_handle->block_size,
		gfarm_import_cb, gfarm_handle);
	return;
finish:
	/* end of this thread */
	gfarm_handle->concurrency_count--;
	if (result != GLOBUS_SUCCESS) {
		gfarm_handle->save_result = result;
	}
	if (gfarm_handle->concurrency_count <= 0 && gfarm_handle->eof) {
		gfs_pio_close(gfarm_handle->gf);
		globus_gridftp_server_finished_transfer(
			op, gfarm_handle->save_result);
		gfarm_handle->done = GLOBUS_TRUE;
	}
	globus_mutex_unlock(&gfarm_handle->mutex);
	return;
}

static void
globus_l_gfs_gfarm_recv(
	globus_gfs_operation_t op,
	globus_gfs_transfer_info_t *transfer_info, void *user_arg)
{
	globus_l_gfs_gfarm_handle_t *gfarm_handle;
	int i;
	mode_t um;
	globus_result_t result;
	gfarm_error_t e;
	GlobusGFSName(globus_l_gfs_gfarm_recv);

	gfarm_handle = (globus_l_gfs_gfarm_handle_t *) user_arg;
	gfarm_handle->done = GLOBUS_FALSE;
	gfarm_handle->eof = GLOBUS_FALSE;
	gfarm_handle->concurrency_count = 0;
	gfarm_handle->offset = 0;
	gfarm_handle->save_result = GLOBUS_SUCCESS;

	um = umask(0022);
	umask(um);
	e = gfs_pio_create(transfer_info->pathname,
			   GFARM_FILE_TRUNC | GFARM_FILE_WRONLY,
			   0666 & ~um,
			   &gfarm_handle->gf);
	if (e != GFARM_ERR_NO_ERROR) {
		result = GlobusGFSErrorSystemError("gfs_pio_create",
						   gfarm_error_to_errno(e));
		globus_gridftp_server_finished_transfer(op, result);
		return;
	}
	globus_gridftp_server_begin_transfer(op, 0, gfarm_handle);

	globus_mutex_lock(&gfarm_handle->mutex);
	result = buffers_initialize(op, gfarm_handle);
	if (result != GLOBUS_SUCCESS)
		goto error;
	for (i = 0; i < gfarm_handle->concurrency; i++) {
		gfarm_handle->concurrency_count++;
		result = globus_gridftp_server_register_read(
			op, gfarm_handle->buffers[i],
			gfarm_handle->block_size,
			gfarm_import_cb, gfarm_handle);
		if (result != GLOBUS_SUCCESS)
			goto error;
	}
	globus_mutex_unlock(&gfarm_handle->mutex);
	return;
error:
	gfarm_handle->done = GLOBUS_TRUE;
	globus_mutex_unlock(&gfarm_handle->mutex);
	gfs_pio_close(gfarm_handle->gf);
	globus_gridftp_server_finished_transfer(op, result);
	return;
}

/*************************************************************************
 *  send
 *  ----
 *  This interface function is called when the client requests to receive
 *  a file from the server.
 *
 *  To send a file to the client the following functions will be used
 *  in roughly the presented order.  They are doced in more detail
 *  with the gridftp server documentation.
 *
 *      globus_gridftp_server_begin_transfer();
 *      globus_gridftp_server_register_write();
 *      globus_gridftp_server_finished_transfer();
 *
 ************************************************************************/
static void
gfarm_export_and_register_write(
	globus_gfs_operation_t op,
	globus_l_gfs_gfarm_handle_t *gfarm_handle, globus_byte_t *buf);

static void
gfarm_export_cb(
	globus_gfs_operation_t op,
	globus_result_t result,
	globus_byte_t *buffer,
	globus_size_t nbytes,
	void *user_arg)
{
	globus_l_gfs_gfarm_handle_t *gfarm_handle;
	GlobusGFSName(gfarm_export_cb);

	gfarm_handle = (globus_l_gfs_gfarm_handle_t *) user_arg;
	/* next */
	gfarm_export_and_register_write(op, gfarm_handle, buffer);
}

static void
gfarm_export_and_register_write(
	globus_gfs_operation_t op,
	globus_l_gfs_gfarm_handle_t *gfarm_handle, globus_byte_t *buf)
{
	int rv;
	globus_result_t result;
	gfarm_error_t e;
	GlobusGFSName(gfarm_read_and_register_write);

	globus_mutex_lock(&gfarm_handle->mutex);
	if (gfarm_handle->done)
		goto end;

	e = gfs_pio_read(gfarm_handle->gf, buf,
			 gfarm_handle->block_size, &rv);
	if (e != GFARM_ERR_NO_ERROR) {
		result = GlobusGFSErrorSystemError(
			"gfs_pio_read", gfarm_error_to_errno(e));
		goto finish;
	}
	if (rv == 0) { /* end */
		result =  GLOBUS_SUCCESS;
		goto finish;
	}
	gfarm_handle->offset += rv;
	/* regist and send */
	result = globus_gridftp_server_register_write(
		op, buf, rv, gfarm_handle->offset,
		-1, gfarm_export_cb, gfarm_handle);
	if (result == GLOBUS_SUCCESS)
		goto end; /* next */
finish:
	globus_gridftp_server_finished_transfer(op, result);
	gfs_pio_close(gfarm_handle->gf);
	gfarm_handle->done = GLOBUS_TRUE;
end:
	globus_mutex_unlock(&gfarm_handle->mutex);
	return;
}

static void
globus_l_gfs_gfarm_send(
	globus_gfs_operation_t op,
	globus_gfs_transfer_info_t *transfer_info,
	void *user_arg)
{
	globus_l_gfs_gfarm_handle_t *gfarm_handle;
	globus_result_t result;
	globus_off_t o;
	int i;
	gfarm_error_t e;
	GlobusGFSName(globus_l_gfs_gfarm_send);

	gfarm_handle = (globus_l_gfs_gfarm_handle_t *) user_arg;
	gfarm_handle->done = GLOBUS_FALSE;
	gfarm_handle->offset = 0;
	e = gfs_pio_open(transfer_info->pathname,
			 GFARM_FILE_RDONLY, &gfarm_handle->gf);
	if (e != GFARM_ERR_NO_ERROR) {
		result = GlobusGFSErrorSystemError("gfs_pio_open",
						   gfarm_error_to_errno(e));
		globus_gridftp_server_finished_transfer(op, result);
		return;
	}
	globus_gridftp_server_get_read_range(
		op, &gfarm_handle->offset, &gfarm_handle->read_len);
	globus_gridftp_server_begin_transfer(op, 0, gfarm_handle);

	e = gfs_pio_seek(gfarm_handle->gf, gfarm_handle->offset,
			 GFARM_SEEK_SET, &o);
	if (e != GFARM_ERR_NO_ERROR) {
		result = GlobusGFSErrorSystemError("gfs_pio_seek",
                                                   gfarm_error_to_errno(e));
		goto error;
	}
	result = buffers_initialize(op, gfarm_handle);
	if (result != GLOBUS_SUCCESS)
		goto error;
	for (i = 0; i < gfarm_handle->concurrency; i++) {
		gfarm_export_and_register_write(op, gfarm_handle,
						gfarm_handle->buffers[i]);
	}
	/* start */
	return;
error:
	globus_gridftp_server_finished_transfer(op, result);
	gfs_pio_close(gfarm_handle->gf);
	return;
}

static int
globus_l_gfs_gfarm_activate(void);

static int
globus_l_gfs_gfarm_deactivate(void);

/*
 *  no need to change this
 */
static globus_gfs_storage_iface_t globus_l_gfs_gfarm_dsi_iface =
{
	GLOBUS_GFS_DSI_DESCRIPTOR_BLOCKING | GLOBUS_GFS_DSI_DESCRIPTOR_SENDER,
	globus_l_gfs_gfarm_start,
	globus_l_gfs_gfarm_destroy,
	NULL, /* list */
	globus_l_gfs_gfarm_send,
	globus_l_gfs_gfarm_recv,
	NULL, /* trev */
	NULL, /* active */
	NULL, /* passive */
	NULL, /* data destroy */
	globus_l_gfs_gfarm_command,
	globus_l_gfs_gfarm_stat,
	NULL,
	NULL
};

/*
 *  no need to change this
 */
GlobusExtensionDefineModule(globus_gridftp_server_gfarm) =
{
	"globus_gridftp_server_gfarm",
	globus_l_gfs_gfarm_activate,
	globus_l_gfs_gfarm_deactivate,
	NULL,
	NULL,
	&local_version
};

/*
 *  no need to change this
 */
static int
globus_l_gfs_gfarm_activate(void)
{
	globus_extension_registry_add(
		GLOBUS_GFS_DSI_REGISTRY,
		"gfarm",
		GlobusExtensionMyModule(globus_gridftp_server_gfarm),
		&globus_l_gfs_gfarm_dsi_iface);
	return (0);
}

/*
 *  no need to change this
 */
static int
globus_l_gfs_gfarm_deactivate(void)
{
	globus_extension_registry_remove(
		GLOBUS_GFS_DSI_REGISTRY, "gfarm");
	return (0);
}
