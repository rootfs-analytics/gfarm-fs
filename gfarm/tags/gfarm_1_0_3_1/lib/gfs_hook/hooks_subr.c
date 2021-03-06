/*
 * $Id$
 */

#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gfarm/gfarm.h>
#include "hooks_subr.h"
/* for creating_file hash -- XXX: Gfarm v2 may make this stuff obsolete */
#include "hash.h"
#include <openssl/evp.h>
#include "gfs_pio.h"

#define MAX_GFS_FILE_BUF	2048

struct _gfs_file_descriptor {
	int refcount;
	unsigned char d_type;   /* file types in <gfarm/gfs.h> */
        union {
		GFS_File f;
		struct {
			GFS_Dir dir;
			int readcount;
			struct gfs_dirent *suspended;
			struct gfs_stat gst;
			char *canonical_path; /* for __fchdir() hook */
		} *d;
	} u;
};
static struct _gfs_file_descriptor *_gfs_file_buf[MAX_GFS_FILE_BUF];

void
gfs_hook_not_initialized(void)
{
	static int printed = 0;

	if (!printed) {
		printed = 1;
		fprintf(stderr,
			"fatal error: gfarm_initialize() isn't called\n");
	}
}

/*
 * gfs_file_buf management
 */

int
gfs_hook_insert_gfs_file(GFS_File gf)
{
	int fd, save_errno;
	struct stat st;

	_gfs_hook_debug(fprintf(stderr, "GFS: insert_gfs_file: %p\n", gf));

	/*
	 * A new file descriptor is needed to identify a hooked file
	 * descriptor.
	 */
	fd = gfs_pio_fileno(gf);
	if (fstat(fd, &st) == -1) {
		save_errno = errno;
		gfs_hook_delete_creating_file(gf);
		gfs_pio_close(gf);
		errno = save_errno;
		return (-1);
	}
	if (S_ISREG(st.st_mode))
		fd = dup(fd);
	else /* don't return a socket, to make select(2) work with this fd */
		fd = open("/dev/null", O_RDWR);
	if (fd == -1) {
		save_errno = errno;
		gfs_hook_delete_creating_file(gf);
		gfs_pio_close(gf);
		errno = save_errno;
		return (-1);
	}
	if (fd >= MAX_GFS_FILE_BUF) {
		__syscall_close(fd);
		gfs_hook_delete_creating_file(gf);
		gfs_pio_close(gf);
		errno = EMFILE;
		return (-1);
	}
	if (_gfs_file_buf[fd] != NULL) {
		__syscall_close(fd);
		gfs_hook_delete_creating_file(gf);
		gfs_pio_close(gf);
		errno = EBADF; /* XXX - something broken */
		return (-1);
	}
	_gfs_file_buf[fd] = malloc(sizeof(*_gfs_file_buf[fd]));
	if (_gfs_file_buf[fd] == NULL) {
		errno = ENOMEM;
		return (-1);
	}
	_gfs_file_buf[fd]->refcount = 1;
	_gfs_file_buf[fd]->d_type = GFS_DT_REG;
	_gfs_file_buf[fd]->u.f = gf;
	return (fd);
}

int
gfs_hook_insert_gfs_dir(GFS_Dir dir, char *url)
{
	int fd, save_errno;
	char *e;

	_gfs_hook_debug(fprintf(stderr, "GFS: insert_gfs_dir: %p\n", dir));

	/*
	 * A new file descriptor is needed to identify a hooked file
	 * descriptor.
	 */
	fd = open("/dev/null", O_RDONLY);
	if (fd == -1) {
		save_errno = errno;
		gfs_closedir(dir);
		errno = save_errno;
		return (-1);
	}
	if (fd >= MAX_GFS_FILE_BUF) {
		__syscall_close(fd);
		gfs_closedir(dir);
		errno = EMFILE;
		return (-1);
	}
	if (_gfs_file_buf[fd] != NULL) {
		__syscall_close(fd);
		gfs_closedir(dir);
		errno = EBADF; /* XXX - something broken */
		return (-1);
	}
        _gfs_file_buf[fd] = malloc(sizeof(*_gfs_file_buf[fd]));
        if (_gfs_file_buf[fd] == NULL) {
		__syscall_close(fd);
		gfs_closedir(dir);
		errno = ENOMEM;
		return (-1);
        }
        _gfs_file_buf[fd]->u.d = malloc(sizeof(*_gfs_file_buf[fd]->u.d));
	if (_gfs_file_buf[fd]->u.d == NULL) {
		__syscall_close(fd);
		gfs_closedir(dir);
		errno = ENOMEM;
		return (-1);
        }
	_gfs_file_buf[fd]->refcount = 1;
	_gfs_file_buf[fd]->d_type = GFS_DT_DIR;
	_gfs_file_buf[fd]->u.d->dir = dir;
	_gfs_file_buf[fd]->u.d->readcount = 0;
	_gfs_file_buf[fd]->u.d->suspended = NULL;
	e = gfs_stat(url, &_gfs_file_buf[fd]->u.d->gst);
	if (e != NULL)
		return (-1);
	e = gfarm_canonical_path(gfarm_url_prefix_skip(url), 
				 &_gfs_file_buf[fd]->u.d->canonical_path);
	if (e != NULL)
		return (-1);
	return (fd);
}

unsigned char
gfs_hook_gfs_file_type(int fd)
{
	return (_gfs_file_buf[fd]->d_type);
}

char *
gfs_hook_clear_gfs_file(int fd)
{
	GFS_File gf;
	char *e = NULL;

	_gfs_hook_debug(fprintf(stderr, "GFS: clear_gfs_file: %d\n", fd));

	gf = gfs_hook_is_open(fd);

	if (--_gfs_file_buf[fd]->refcount > 0) {
	  	/* fd is duplicated, skip closing the file. */
		_gfs_hook_debug(fprintf(stderr,
					"GFS: clear_gfs_file: skipped\n"));
	} else {
		if (gfs_hook_gfs_file_type(fd) == GFS_DT_REG) {
			gfs_hook_delete_creating_file(gf);
			e = gfs_pio_close(gf);
		} else if (gfs_hook_gfs_file_type(fd) == GFS_DT_DIR) {
			_gfs_file_buf[fd]->u.d->dir = NULL;
			_gfs_file_buf[fd]->u.d->readcount = 0;
			_gfs_file_buf[fd]->u.d->suspended = NULL;
			gfs_stat_free(&_gfs_file_buf[fd]->u.d->gst);
			free(_gfs_file_buf[fd]->u.d->canonical_path); 
			free(_gfs_file_buf[fd]->u.d);
			e = gfs_closedir((GFS_Dir)gf);
		}

		free(_gfs_file_buf[fd]);
	}

	__syscall_close(fd);
	_gfs_file_buf[fd] = NULL;
	return (e);
}

struct _gfs_file_descriptor *gfs_hook_dup_descriptor(int fd)
{
	if (gfs_hook_is_open(fd) == NULL)
		return (NULL);
	++_gfs_file_buf[fd]->refcount;
	return (_gfs_file_buf[fd]);
}

void gfs_hook_set_descriptor(int fd, struct _gfs_file_descriptor *d)
{
	if (gfs_hook_is_open(fd) != NULL)
		gfs_hook_clear_gfs_file(fd);
	_gfs_file_buf[fd] = d;
}

#if 0
int
gfs_hook_dup_filedes(int oldfd, int newfd)
{
	_gfs_hook_debug(
	   fprintf(stderr, "GFS: dpu_filedes: %d, %d\n", oldfd, newfd));

#if 0
	if (_gfs_file_buf[oldfd] == _gfs_file_buf[newfd])
		return (newfd);		
#endif
		_gfs_file_buf[newfd] = _gfs_file_buf[oldfd];
	}

	return (newfd);
}
#endif

/*  printf and puts should not be put into the following function. */
void *
gfs_hook_is_open(int fd)
{
	if (fd < 0 || fd >= MAX_GFS_FILE_BUF)
		return (NULL);

	if (_gfs_file_buf[fd] == NULL)
		return (NULL);

	switch (gfs_hook_gfs_file_type(fd)) {
	case GFS_DT_REG:
		return (_gfs_file_buf[fd]->u.f);
	case GFS_DT_DIR:
		return (_gfs_file_buf[fd]->u.d->dir);
	default:
		return (NULL);
	}
}

/*
 * hash table for files under creation
 *
 * This hash maintains a list of filenames that is created by this
 * process but not closed yet.  This hash is needed to 'stat' these
 * files in Gfarm v1 API.
 */
#define CREATING_FILE_HASHTAB_SIZE	53	/* prime number */
static struct gfarm_hash_table *creating_file_hashtab = NULL;

char *
gfs_hook_add_creating_file(GFS_File gf)
{
	struct gfarm_hash_entry *entry;
	int created;

	_gfs_hook_debug(fprintf(stderr, "GFS: add_creating_file: %p (%s)\n",
				gf, gf->pi.pathname));

	if (creating_file_hashtab == NULL) {
		creating_file_hashtab =
		    gfarm_hash_table_alloc(CREATING_FILE_HASHTAB_SIZE,
			gfarm_hash_default, gfarm_hash_key_equal_default);
		if (creating_file_hashtab == NULL)
			return (GFARM_ERR_NO_MEMORY);
	}

	entry = gfarm_hash_enter(creating_file_hashtab,
	    gf->pi.pathname, strlen(gf->pi.pathname) + 1,
	    sizeof(GFS_File), &created);
	if (entry == NULL)
		return (GFARM_ERR_NO_MEMORY);

	/* always update */
	*(GFS_File *)gfarm_hash_entry_data(entry) = gf;
	return (NULL);
}

GFS_File
gfs_hook_is_now_creating(const char *url)
{
	char *pathname, *e;
	struct gfarm_hash_entry *entry;

	_gfs_hook_debug(fprintf(stderr, "GFS: is_now_creating: %s\n", url));

	if (creating_file_hashtab == NULL)
		return (NULL);

	e = gfarm_url_make_path(url, &pathname);
	if (e != NULL)
		return (NULL);

	_gfs_hook_debug(fprintf(stderr, "GFS: is_now_creating: %s\n",
				pathname));

	entry = gfarm_hash_lookup(creating_file_hashtab,
	    pathname, strlen(pathname) + 1);
	free(pathname);
	if (entry == NULL)
		return (NULL);
	else
		return *(GFS_File *)gfarm_hash_entry_data(entry);
}

void
gfs_hook_delete_creating_file(GFS_File gf)
{
	_gfs_hook_debug(fprintf(stderr, "GFS: delete_creating_file: %p (%s)\n",
				gf, gf->pi.pathname));

	if (creating_file_hashtab == NULL)
		return;

	gfarm_hash_purge(creating_file_hashtab, 
	    gf->pi.pathname, strlen(gf->pi.pathname) + 1);
	return;
}

void
gfs_hook_inc_readcount(int fd)
{
	_gfs_hook_debug(
		fprintf(stderr, "GFS: inc_readount: %d\n", fd));

	++_gfs_file_buf[fd]->u.d->readcount;
}

int
gfs_hook_is_read(int fd)
{
	return (_gfs_file_buf[fd]->u.d->readcount > 0);
}

void
gfs_hook_set_suspended_gfs_dirent(int fd, struct gfs_dirent *entry)
{
	_gfs_file_buf[fd]->u.d->suspended = entry;
}

struct gfs_dirent *
gfs_hook_get_suspended_gfs_dirent(int fd)
{
	return (_gfs_file_buf[fd]->u.d->suspended);
}

struct gfs_stat *
gfs_hook_get_gfs_stat(int fd)
{
	return (&_gfs_file_buf[fd]->u.d->gst);
}

char *
gfs_hook_get_gfs_canonical_path(int fd)
{
	return (_gfs_file_buf[fd]->u.d->canonical_path);
}

/*
 *  Check whether pathname is gfarm url or not.
 *
 *  Gfarm URL:  gfarm:[:section:]pathname
 *
 *  It is necessary to free the memory space for *url.
 *  Also, it's necessary to free the memory space for *secp,
 *  if *secp is not NULL.
 */
static char gfs_mntdir[] = "/gfarm";
extern int gfs_hook_cwd_is_gfarm;
static char *received_prefix = NULL;

static int
gfs_hook_is_null_or_slash(const char c)
{
	return (c == '\0' || c == '/' || c == ':');
}

static int
gfs_hook_is_mount_point(const char *path)
{
	return (*path == '/' &&
		memcmp(path, gfs_mntdir, sizeof(gfs_mntdir) - 1) == 0 &&
		gfs_hook_is_null_or_slash(path[sizeof(gfs_mntdir) - 1]));
}

static int
set_received_prefix(const char *path)
{
	char *end, *p;
	int len;

	if (received_prefix != NULL) {
		free(received_prefix);
		received_prefix = NULL;
	}
	if (gfs_hook_is_mount_point(path)) {
		received_prefix = strdup(gfs_mntdir);
		if (received_prefix == NULL)
			return (0); /* XXX - should return ENOMEM */
		return (1);
	}
	/* path is either 'gfarm:' or 'gfarm@'. */
	if ((end = strchr(path, ':')) == NULL)
		end = strchr(path, '@');
	if (end == NULL)
		return (0); /* XXX */

	len = end - path + 1;
	p = malloc(len + 1);
	if (p == NULL)
		return (0); /* XXX - should return ENOMEM */
	received_prefix = p;
	strncpy(received_prefix, path, len);
	received_prefix[len] = '\0';
	return (1);
}

/*
 * Gfarm path starts with '<mount_point>', 'gfarm:' or 'gfarm@'.
 *
 * When the Gfarm path starts with '<mount_point>', the path is
 * considered to be an absolute path instead of a relative path.
 *
 * '<mount_point>/~username' such as '/gfarm/~tatebe' can be used to
 * specify a home directory in Gfarm file system.
 */
int
gfs_hook_is_url(const char *path, char **urlp, char **secp)
{
	static char prefix[] = "gfarm:";
	int sizeof_gfarm_prefix = sizeof(prefix);
	int sizeof_prefix = sizeof_gfarm_prefix;
	const char *path_save;
	int is_mount_point = 0, remove_slash = 0;
	/*
	 * ROOT patch:
	 *   'gfarm@' is also considered as a Gfarm URL
	 */
	static char gfarm_url_prefix_for_root[] = "gfarm@";

	*secp = NULL;
	path_save = path;
	if (gfs_hook_is_mount_point(path)) {
		is_mount_point = 1;
		sizeof_prefix = sizeof(gfs_mntdir);
	}
	if (is_mount_point || gfarm_is_url(path) ||
	    /* ROOT patch */
	    memcmp(path, gfarm_url_prefix_for_root,
	    sizeof(gfarm_url_prefix_for_root) - 1) == 0) {
		if (!gfarm_initialized &&
		    gfs_hook_initialize() != NULL) {
			gfs_hook_not_initialized();
			return (0); /* don't perform gfarm operation */
		}
		/*
		 * extension for accessing individual sections
		 *   gfarm::section:pathname
		 */
		if (path[sizeof_prefix - 1] == ':') {
			const char *p = path + sizeof_prefix;
			int secsize = strcspn(p, "/:");
			int urlsize;

			if (p[secsize] != ':')
				return (0); /* gfarm::foo/:bar or gfarm::foo */

			/* '/gfarm/~' will be translated to 'gfarm:~'. */
			if (is_mount_point
			    && p[secsize + 1] == '/'
			    && p[secsize + 2] == '~')
				remove_slash = 1;

			urlsize = sizeof_gfarm_prefix - 1
				+ strlen(p + secsize + remove_slash + 1);
			*urlp = malloc(urlsize + 1);
			*secp = malloc(secsize + 1);
			if (*urlp == NULL || *secp == NULL) {
				if (*urlp != NULL)
					free(*urlp);
				if (*secp != NULL)
					free(*secp);
				return (0); /* XXX - should return ENOMEM */
			}
			memcpy(*urlp, prefix, sizeof_gfarm_prefix - 1);
			strcpy(*urlp + sizeof_gfarm_prefix - 1,
			       p + secsize + remove_slash + 1);
			memcpy(*secp, p, secsize);
			(*secp)[secsize] = '\0';
			/*
			 * This case needs to free memory space of
			 * both *urlp and *secp.
			 */
		}
		else {
			/* '/gfarm/~' will be translated to 'gfarm:~'. */
			if (is_mount_point
			    && path[sizeof_prefix - 1] == '/'
			    && path[sizeof_prefix] == '~')
				remove_slash = 1;

			*urlp = malloc(sizeof_gfarm_prefix - 1
				       + strlen(path + sizeof_prefix
						+ remove_slash - 1) + 1);
			if (*urlp == NULL)
				return (0) ; /* XXX - should return ENOMEM */
			/*
			 * the reason why we don't just call strcpy(*url, path)
			 * is because the path may be "gfarm@path/name".
			 * (ROOT patch)
			 */
			memcpy(*urlp, prefix, sizeof_gfarm_prefix - 1);
			strcpy(*urlp + sizeof_gfarm_prefix - 1,
			    path + sizeof_prefix + remove_slash - 1);
		}
		if (!set_received_prefix(path_save))
			return (0);
		return (1);
	}
	if (gfs_hook_cwd_is_gfarm && *path_save != '/') {
		*urlp = malloc(strlen(prefix) + strlen(path_save) + 1);
		if (*urlp == NULL)
			return (0) ; /* XXX - should return ENOMEM */
		memcpy(*urlp, prefix, sizeof_prefix - 1);
		strcpy(*urlp + sizeof_prefix - 1, path_save);
		return (1);
	}
	return (0);
}

char *
gfs_hook_get_prefix(char *buf, size_t size)
{
	if (received_prefix == NULL)
		return GFARM_ERR_GFARM_URL_PREFIX_IS_MISSING;
	if (size < strlen(received_prefix))
		return GFARM_ERR_NUMERICAL_RESULT_OUT_OF_RANGE;
	strcpy(buf, received_prefix);
	return (NULL);
}	

/*
 * default file view manipulation
 */

enum gfs_hook_file_view _gfs_hook_default_view = local_view;
int _gfs_hook_index = 0;
int _gfs_hook_num_fragments = GFARM_FILE_DONTCARE;

void
gfs_hook_set_default_view_local()
{
	_gfs_hook_default_view = local_view;
}

void
gfs_hook_set_default_view_index(int index, int nfrags)
{
	_gfs_hook_default_view = index_view;
	_gfs_hook_index = index;
	_gfs_hook_num_fragments = nfrags;
}

void
gfs_hook_set_default_view_global()
{
	_gfs_hook_default_view = global_view;
}

char *
gfs_hook_set_view_local(int filedes, int flag)
{
	GFS_File gf;
	char *e;

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return "not a Gfarm file";

	if ((e = gfs_pio_set_view_local(gf, flag)) != NULL) {
		_gfs_hook_debug(fprintf(stderr,
			"GFS: set_view_local: %s\n", e));
		return e;
	}
	return NULL;
}

char *
gfs_hook_set_view_index(int filedes, int nfrags, int index, 
			char *host, int flags)
{
	GFS_File gf;
	char *e;

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return "not a Gfarm file";

	if ((e = gfs_pio_set_view_index(gf, nfrags, index, host, flags))
	    != NULL) {
		_gfs_hook_debug(fprintf(stderr,
			"GFS: set_view_index: %s\n", e));
		return e;
	}
	return NULL;
}

char *
gfs_hook_set_view_global(int filedes, int flags)
{
	GFS_File gf;
	char *e;

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return "not a Gfarm file";

	if ((e = gfs_pio_set_view_global(gf, flags)) != NULL) {
		_gfs_hook_debug(fprintf(stderr,
			"GFS: set_view_global: %s\n", e));
		return e;
	}
	return NULL;
}
