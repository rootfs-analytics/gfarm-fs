/*
  GfarmFS-FUSE

  Gfarm:
    http://datafarm.apgrid.org/

  FUSE:
    http://fuse.sourceforge.net/

  Mount:
    $ ./gfarmfs [GfarmFS options] <mountpoint> [FUSE options]

  Unmount:
    $ fusermount -u <mountpoint>

  Copyright (c) 2005-2006 National Institute of Advanced Industrial
  Science and Technology (AIST).  All Rights Reserved.
*/
#if FUSE_USE_VERSION >= 25
/* #  warning FUSE 2.5 compatible mode. */
#elif FUSE_USE_VERSION == 22
/* #  warning FUSE 2.2 compatible mode. */
#define ENABLE_FASTCREATE 1
#else
#  error Please install FUSE 2.2 or later
#endif
#include <fuse.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <libgen.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#if USE_GFS_STATFSNODE
#include <sys/statfs.h>
#endif

#include <gfarm/gfarm.h>
/* These definitions may conflict with <gfarm/gfarm_config.h> */
#ifdef PACKAGE_NAME
#undef PACKAGE_NAME
#endif
#ifdef PACKAGE_STRING
#undef PACKAGE_STRING
#endif
#ifdef PACKAGE_TARNAME
#undef PACKAGE_TARNAME
#endif
#ifdef PACKAGE_VERSION
#undef PACKAGE_VERSION
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define GFARMFS_VER  PACKAGE_VERSION
#define GFARMFS_VER_DATE "November , 2006"

#ifndef GFS_DEV
#define GFS_DEV ((dev_t)-1);
#endif
#ifndef GFS_BLKSIZE
#define GFS_BLKSIZE 8192
#endif
#ifndef STAT_BLKSIZ
#define STAT_BLKSIZ 512
#endif

#define SYMLINK_MODE 1
#ifdef  SYMLINK_MODE
#define SYMLINK_SUFFIX ".gfarmfs-symlink"
#define SYMLINK_SUFFIX_LEN (sizeof(SYMLINK_SUFFIX)-1)
#endif

#define GFARM_USE_VERSION 1

#if GFARM_USE_VERSION == 1
#define REVISE_UTIME 1  /* problem of gfarm v1 */
#endif

/* limitation in global view of gfarm v1.3.1 or earlier */
#define REVISE_CHMOD 0

#define ENABLE_NOFLAGMENTINFO_AUTO_DELETE 0

#define ENABLE_ASYNC_REPLICATION 1

#define ENABLE_XATTR 0

/* ################################################################### */

static int gfarmfs_debug = 0;  /* 1: error, 2: debug */
static int enable_symlink = 0;
static int enable_linkiscopy = 0;
static int enable_unlinkall = 0;
static char *arch_name = NULL;
static int enable_count_dir_nlink = 0; /* default: disable */
static FILE *enable_trace = NULL;
static char *trace_name = NULL; /* filename */
static FILE *enable_errlog = NULL;
static char *errlog_name = NULL; /* filename */
static FILE *errlog_out;
static char *gfarm_mount_point = "";
static int enable_gfarm_iobuf = 0; /* about GFARM_FILE_UNBUFFERED */
static int enable_exact_filesize = 0;

#if ENABLE_ASYNC_REPLICATION
#define REP_DISABLE  (0)
#define REP_GFREP_N  (1)  /* gfrep -N */
#if USE_GFREP_H
#define REP_GFREP_H  (2)  /* gfrep -H */
#endif
#if USE_GFARM_SCRAMBLE
#define REP_SCRAMBLE (3)
#endif
static int enable_replication = REP_DISABLE;
static char *gfrep_path = NULL;
static int gfrep_target_num = -1;
static char *gfrep_dom = NULL;
#if USE_GFREP_H
static int force_gfrep_N = 0;
#define FIX_GFREP_H 1 /* for executable file in gfarm v1.4 or earlier */
#endif

#endif /* ENABLE_ASYNC_REPLICATION */

/* 0: use gfarmfs_*_share_gf() operations (new)
 * 1: use normal I/O operations (old)
 */
static int use_old_functions = 0;

/* default: enable */
static int enable_fastcreate = 1;  /* used on FUSE version 2.2 only */
                                   /* >0: enable, 0: disable, <0: ignore */
#if USE_GFS_STATFSNODE
static int enable_statfs = 1; /* default: enable */
#endif

/* ################################################################### */

static int gmplen = -1;

static char *
add_gfarm_prefix(const char *path, char **urlp)
{
	char *url;
	int len;

	if (gmplen == -1)
		gmplen = strlen(gfarm_mount_point);
	len = gmplen + strlen(path) + 7;
	url = malloc(sizeof(char) * len);
	if (url == NULL)
		return (GFARM_ERR_NO_MEMORY);
	snprintf(url, len, "gfarm:%s%s", gfarm_mount_point, path);
	*urlp = url;
	return (NULL);
}

#ifdef SYMLINK_MODE
static char *
add_gfarm_prefix_symlink_suffix(const char *path, char **urlp)
{
	char *url;
	int len;

	if (gmplen == -1)
		gmplen = strlen(gfarm_mount_point);
	len = gmplen + strlen(path) + 7 + SYMLINK_SUFFIX_LEN;
	url = malloc(sizeof(char) * len);
	if (url == NULL)
                return (GFARM_ERR_NO_MEMORY);
	snprintf(url, len, "gfarm:%s%s%s", gfarm_mount_point, path,
		 SYMLINK_SUFFIX);
	*urlp = url;
	return (NULL);
}
#endif

#define gfarm_url2path(url)  (url + 6)  /* cut "gfarm:" */

static void
gfarmfs_errlog(const char *format, ...)
{
	va_list ap;
	struct timeval tv;
	struct tm *lt;

	va_start(ap, format);
	gettimeofday(&tv, NULL);
	lt = localtime(&tv.tv_sec);
	fprintf(errlog_out, "[%.2d-%.2d %.2d:%.2d:%.2d] ", lt->tm_mon + 1,
		lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec);
	vfprintf(errlog_out, format, ap);
	fprintf(errlog_out, "\n");
	va_end(ap);
}

#if 0
static char *
gfarmfs_init()
{
	return (NULL);
}
#else
#define gfarmfs_init() (NULL)
#endif

static char OP_RELEASE[] = "RELEASE";

static int
gfarmfs_final(char *opname, char *e, int val_noerror, const char *name)
{
	int return_errno;

	if (e == NULL) {
		if (enable_trace != NULL)
			fprintf(enable_trace, "OK: %s: %s\n", opname, name);
		return (val_noerror);
	} else {
		if (enable_trace != NULL)
			fprintf(enable_trace, "NG: %s: %s: %s\n",
				opname, name, e);
		return_errno = gfarm_error_to_errno(e);
		if (return_errno == EINVAL || opname == OP_RELEASE)
			gfarmfs_errlog("%s: %s: %s", opname, name, e);
		return -return_errno;
	}
}

static int
gfarmfs_dir_nlink(const char *url)
{
	GFS_Dir dir;
	struct gfs_dirent *entry;
	char *e;
	int res = 0;

	if (enable_count_dir_nlink == 0) {
		return (32000);
	}

	e = gfs_opendir(url, &dir);
	if (e == NULL) {
		while ((e = gfs_readdir(dir, &entry)) == NULL &&
		       entry != NULL) {
			if (S_ISDIR(DTTOIF(entry->d_type))) {
				res++;
			}
		}
		gfs_closedir(dir);
	}
	if (res == 0) {
		return (2);
	} else {
		return (res);
	}
}

#ifdef SYMLINK_MODE
static int
ends_with_and_delete(char *str, char *suffix)
{
	int m, n;

	m = strlen(str) - 1;
	n = strlen(suffix) - 1;
	while (n >=0) {
		if (m <= 0 || str[m] != suffix[n]) {
			return (0); /* false */
		}
		m--;
		n--;
	}
	str[m + 1] = '\0';
	return (1); /* true */
}
#endif

#ifdef USE_GFARM_SCRAMBLE
/* for scramble version */
#define gfarmfs_set_view_using_url(gf, url)   gfs_pio_set_view_scramble(gf, 0)
#define gfarmfs_set_view_using_mode(gf, mode) gfs_pio_set_view_scramble(gf, 0)

#else
/* for normal version */
static char *
gfarmfs_set_view(GFS_File gf)
{
	char *e;
	int nf;

	e = gfs_pio_get_nfragment(gf, &nf);
	if (e == NULL && nf <= 1)
		e = gfs_pio_set_view_index(gf, 1, 0, NULL, 0);
	else /* GFARM_ERR_FRAGMENT_INDEX_NOT_AVAILABLE -> creating new file */
		e = gfs_pio_set_view_global(gf, 0);
	return (e);
}

static char *
gfarmfs_set_view_using_url(GFS_File gf, char *url)
{
	char *e;

	if (arch_name != NULL) {
		e = gfs_access(url, X_OK);
		if (e == NULL) {
			e = gfs_pio_set_view_section(gf, arch_name, NULL, 0);
			return (e);
		}
	}
	return gfarmfs_set_view(gf);
}

static char *
gfarmfs_set_view_using_mode(GFS_File gf, mode_t mode)
{
	char *e;

	if (arch_name != NULL && GFARM_S_IS_PROGRAM(mode)) {
		e = gfs_pio_set_view_section(gf, arch_name, NULL, 0);
		return (e);
	}
	return gfarmfs_set_view(gf);
}
#endif

static char *
gfarmfs_create_empty_file(const char *path, mode_t mode)
{
	char *e, *e2;
	char *url;
	GFS_File gf;

	e = add_gfarm_prefix(path, &url);
	if (e != NULL)
		return (e);
	e = gfs_pio_create(url,
			   GFARM_FILE_WRONLY|GFARM_FILE_TRUNC|
			   GFARM_FILE_EXCLUSIVE,
			   mode, &gf);
	if (e == NULL) {
		e2 = gfarmfs_set_view_using_mode(gf, mode);
		e = gfs_pio_close(gf);
		if (e2 != NULL) e = e2;
	}
	free(url);
	return (e);
}

/* ################################################################### */
/* FUSE version 2.2 only */
#ifdef ENABLE_FASTCREATE

struct fastcreate {
	char *path;
	mode_t mode;
};

static struct fastcreate fc = {NULL, 0};

static void
gfarmfs_fastcreate_free()
{
	if (fc.path != NULL) {
		free(fc.path);
		fc.path = NULL;
	}
}

static char *
gfarmfs_fastcreate_flush()
{
	if (fc.path != NULL) {
		char *e;

		if (gfarmfs_debug >= 2) {
			printf("FASTCREATE: flush: %s\n", fc.path);
		}
		e = gfarmfs_create_empty_file(fc.path, fc.mode);
		if (e != NULL) {
			gfarmfs_errlog("FASTCREATE: flush: %s: %s",
				       fc.path, e);
		}
		gfarmfs_fastcreate_free();
		return (e);
	} else {
		return (NULL);  /* do nothing */
	}
}

static char *
gfarmfs_fastcreate_save(const char *path, mode_t mode)
{
	gfarmfs_fastcreate_flush();

	fc.path = strdup(path);
	if (fc.path == NULL) {
		return (GFARM_ERR_NO_MEMORY);
	}
	fc.mode = mode;
	if (gfarmfs_debug >= 2) {
		printf("FASTCREATE: add: %s\n", path);
	}
	return (NULL);
}

static char *
gfarmfs_fastcreate_open(char *url, int flags, GFS_File *gfp)
{
	char *e;
	char *path = gfarm_url2path(url);

	if (fc.path != NULL && strcmp(fc.path, path) == 0) {
		if (gfarmfs_debug >= 2) {
			printf("FASTCREATE: open: %s\n", path);
		}
		e = gfs_pio_create(url,
				   flags|GFARM_FILE_TRUNC|GFARM_FILE_EXCLUSIVE,
				   fc.mode, gfp);
		if (e == NULL) {
			e = gfarmfs_set_view_using_mode(
				*gfp, fc.mode);
			if (e != NULL)
				gfs_pio_close(*gfp);
		}
		gfarmfs_fastcreate_free();
	} else {
		gfarmfs_fastcreate_flush(); /* flush previous saved path */
		e = gfs_pio_open(url, flags, gfp);
		if (e == NULL) {
			e = gfarmfs_set_view_using_url(*gfp, url);
			if (e != NULL)
				gfs_pio_close(*gfp);
		}
	}
	return (e);
}

static int
gfarmfs_fastcreate_getattr(const char *path, struct stat *buf)
{
	time_t now;

	if (fc.path != NULL && strcmp(fc.path, path) == 0) {
		memset(buf, 0, sizeof(struct stat));
		buf->st_dev = GFS_DEV;
		buf->st_ino = 12345;    /* XXX */
		buf->st_mode = fc.mode;
		buf->st_nlink = 1;
		buf->st_uid = getuid();
		buf->st_gid = getgid();
		buf->st_size = 0;
		buf->st_blksize = GFS_BLKSIZE;
		buf->st_blocks = 0;
		time(&now);
		buf->st_atime = buf->st_mtime = buf->st_ctime = now;
		return (1);
	} else {
		return (0);
	}
}

#define gfarmfs_fastcreate_check() \
(enable_fastcreate > 0 ? (gfarmfs_fastcreate_flush() == NULL ? 1 : -1) : 0)

#else
#define gfarmfs_fastcreate_check()
#endif /* ENABLE_FASTCREATE */

/* ################################################################### */

static char *
convert_gfs_stat_to_stat(const char *url,
			 struct gfs_stat *gsp, struct stat *stp, int symlink)
{
	/* referred to hooks_stat.c */
	struct passwd *p;
	static struct passwd *p_save = NULL;
	static char *username_save = NULL;

	memset(stp, 0, sizeof(struct stat));
	stp->st_dev = GFS_DEV;
	stp->st_ino = gsp->st_ino;
	stp->st_mode = gsp->st_mode;
#ifdef SYMLINK_MODE
	if (symlink == 1) {
		stp->st_mode = 0777 | S_IFLNK;
	}
#endif
	if (url != NULL && GFARM_S_ISDIR(stp->st_mode)) {
		stp->st_nlink = gfarmfs_dir_nlink(url);
	} else {
		stp->st_nlink = 1;
	}

	if (gsp->st_user != NULL && username_save != NULL &&
	    strcmp(gsp->st_user, username_save) == 0) {
		stp->st_uid = p_save->pw_uid;
		stp->st_gid = p_save->pw_gid;
	} else if (gsp->st_user != NULL &&
		   ((p = getpwnam(gsp->st_user)) != NULL)) {
		stp->st_uid = p->pw_uid;
		stp->st_gid = p->pw_gid;
		if (username_save != NULL)
			free(username_save);
		username_save = strdup(gsp->st_user);
		p_save = p;
	} else {
		stp->st_uid = getuid();
		stp->st_gid = getgid();
	}

	stp->st_size = gsp->st_size;
	stp->st_blksize = GFS_BLKSIZE;
	stp->st_blocks = (gsp->st_size + STAT_BLKSIZ - 1) / STAT_BLKSIZ;
	stp->st_atime = gsp->st_atimespec.tv_sec;
	stp->st_mtime = gsp->st_mtimespec.tv_sec;
	stp->st_ctime = gsp->st_ctimespec.tv_sec;

	return (NULL);
}

static char *
gfarmfs_exact_filesize(char *url, file_offset_t *sizep, mode_t mode)
{
	/* get st_size using gfs_fstat */
	/* gfs_stat cannot get the exact st_size while the file is opened.
	 * But gfs_fstat can do it.
	 */
	GFS_File gf;
	int flags;
	char *e;
	struct gfs_stat gs;
	file_offset_t st_size;
	int change_mode = 0;
	mode_t save_mode = 0;

	if (mode & 0444) {
		flags = GFARM_FILE_RDONLY;
#if 0
	} else if (mode & 0200) {
		flags = GFARM_FILE_WRONLY;
#endif
	} else {
		save_mode = mode;
		e = gfs_chmod(url, mode|0400);
		if (e != NULL) {
			gfarmfs_errlog("GETATTR: cancel fstat: %s: %s",
				       gfarm_url2path(url), e);
			return (e); /* not my modifiable file */
		}
		change_mode = 1;
		flags = GFARM_FILE_RDONLY;
	}
	e = gfs_pio_open(url, flags, &gf);
	if (e != NULL) goto revert_mode;
#ifdef USE_GFARM_SCRAMBLE
	e = gfs_pio_set_view_scramble(gf, 0);
	if (e != NULL) goto fstat_close;
	e = gfs_fstat(gf, &gs);
	if (e != NULL) goto fstat_close;
	st_size = gs.st_size;
	gfs_stat_free(&gs);
#else
	if (GFARM_S_IS_PROGRAM(mode)) {
		if (arch_name == NULL) {
			e = gfs_pio_set_view_global(gf, 0);
			if (e != NULL) goto fstat_close;
		} else {
			e = gfs_pio_set_view_section(gf, arch_name, NULL, 0);
		}
		if (e != NULL) goto fstat_close;
		e = gfs_fstat(gf, &gs);
		if (e != NULL) goto fstat_close;
		st_size = gs.st_size;
		gfs_stat_free(&gs);
	} else {
		int nf, i;
		e = gfs_pio_get_nfragment(gf, &nf);
		if (e != NULL) goto fstat_close;
		st_size = 0;
		for (i = 0; i < nf; i++) {
			e = gfs_pio_set_view_index(gf, nf, i, NULL, 0);
			if (e != NULL) goto fstat_close;
			e = gfs_fstat(gf, &gs);
			if (e != NULL) goto fstat_close;
			st_size += gs.st_size;
			gfs_stat_free(&gs);
		}
	}
#endif
	/* success */
	*sizep = st_size;
fstat_close:
	gfs_pio_close(gf);
	if (e != NULL && e != GFARM_ERR_NO_SUCH_OBJECT)
		gfarmfs_errlog("exact_filesize: %s: %s",
			       gfarm_url2path(url), e);
revert_mode:
	if (change_mode == 1) {
		e = gfs_chmod(url, save_mode);
		if (e != NULL)
			gfarmfs_errlog("exact_filesize: "
				       "revert st_mode (%o): %s: %s",
				       save_mode, gfarm_url2path(url), e);
	}
	return (e);
}

static int
gfarmfs_getattr(const char *path, struct stat *buf)
{
	struct gfs_stat gs;
	char *e;
	char *url;
	int symlinkmode = 0;

	if ((e = gfarmfs_init()) != NULL) goto end;
#ifdef ENABLE_FASTCREATE
	if (enable_fastcreate > 0 && gfarmfs_fastcreate_getattr(path, buf)) {
		goto end;
	}
#endif
	e = add_gfarm_prefix(path, &url);
	if (e != NULL)
		goto end;
	e = gfs_stat(url, &gs);
#ifdef SYMLINK_MODE
	if (enable_symlink == 1 && e == GFARM_ERR_NO_SUCH_OBJECT) {
		free(url);
		e = add_gfarm_prefix_symlink_suffix(path, &url);
		if (e != NULL)
			goto end;
		if (gfarmfs_debug >= 2) {
			printf("GETATTR: for symlink: %s\n", gfarm_url2path(url));
		}
		e = gfs_stat(url, &gs);
		symlinkmode = 1;
	}
#endif
	if (enable_exact_filesize == 1 && e == NULL &&
	    GFARM_S_ISREG(gs.st_mode)) {
		char *e2;
		file_offset_t size;
		e2 = gfarmfs_exact_filesize(url, &size, gs.st_mode);
		if (e2 == NULL)
			gs.st_size = size;
	}
#if ENABLE_NOFLAGMENTINFO_AUTO_DELETE == 1 /* to delete invalid path_info */
	if (e == NULL &&
	    !GFARM_S_ISDIR(gs.st_mode) &&
	    !GFARM_S_ISREG(gs.st_mode)) {
		struct gfarm_path_info pi;
		char *e2;
		char *p;
		printf("GETATTR: repair invalid entry: %s\n", path);
		e2 = gfarm_canonical_path(path, &p);
		if (e2 == NULL) {
			e2 = gfarm_path_info_get(p, &pi);
			if (e2 == NULL) {
				pi.status.st_mode = 0100600;
				e2 = gfarm_path_info_replace(p, &pi);
			}
			free(p);
		}
	}
	if (e == GFARM_ERR_NO_FRAGMENT_INFORMATION) {
		char *e2;
		e2 = gfs_unlink(url);
		printf("GETATTR: unlink: ");
		if (e2 == NULL) {
			printf("%s\n", path);
		} else {
			printf("%s: %s\n", path, e2);
		}
		e = GFARM_ERR_NO_SUCH_OBJECT;
	}
#endif
	if (e == NULL) {
		e = convert_gfs_stat_to_stat(url, &gs, buf, symlinkmode);
		gfs_stat_free(&gs);
	}
	free(url);
end:
	return gfarmfs_final("GETATTR", e, 0, path);
}

static int
gfarmfs_getdir(const char *path, fuse_dirh_t h, fuse_dirfil_t filler)
{
	GFS_Dir dir;
	struct gfs_dirent *entry;
	char *e;
	char *url;

	gfarmfs_fastcreate_check();
	e = gfarmfs_init();
	if (e == NULL) {
		e = add_gfarm_prefix(path, &url);
		if (e == NULL) {
			e = gfs_opendir(url, &dir);
			free(url);
		}
	}
	if (e == NULL) {
		while ((e = gfs_readdir(dir, &entry)) == NULL &&
		       entry != NULL) {
#ifdef SYMLINK_MODE
			if (enable_symlink == 1 &&
			    ends_with_and_delete(entry->d_name,
						 SYMLINK_SUFFIX) > 0) {
				entry->d_type = DT_LNK;
			}
#endif
			if (filler(h, entry->d_name, entry->d_type,
				   entry->d_ino) != 0) break;
		}
		e = gfs_closedir(dir);
	}

	return gfarmfs_final("GETDIR", e, 0, path);
}

static int
gfarmfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	char *e;

	if ((e = gfarmfs_init()) != NULL) goto end;

	if (S_ISREG(mode)) {
#ifdef ENABLE_FASTCREATE
		if (enable_fastcreate > 0) {
			e = gfarmfs_fastcreate_save(path, mode);
		} else {
			e = gfarmfs_create_empty_file(path, mode);
		}
#else
		e = gfarmfs_create_empty_file(path, mode);
#endif
	} else {
		gfarmfs_errlog(
			"MKNOD: not supported: %s: mode=%o, rdev=%o",
			path, mode, (int)rdev);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	}
end:
	return gfarmfs_final("MKNOD", e, 0, path);
}

static int
gfarmfs_mkdir(const char *path, mode_t mode)
{
	char *e;
	char *url;

	gfarmfs_fastcreate_check();
	e = gfarmfs_init();
	if (e == NULL) {
		e = add_gfarm_prefix(path, &url);
		if (e == NULL) {
			e = gfs_mkdir(url, mode);
			free(url);
		}
	}

	return gfarmfs_final("MKDIR", e, 0, path);
}

static int
gfarmfs_unlink(const char *path)
{
	char *e;
	char *url;

	gfarmfs_fastcreate_check();
	if ((e = gfarmfs_init()) != NULL) goto end;

	if (enable_unlinkall == 1) { /* remove an entry (all architecture) */
		e = add_gfarm_prefix(path, &url);
		if (e != NULL) goto end;
		e = gfs_unlink(url);
		free(url);
#ifdef SYMLINK_MODE
		if (enable_symlink == 1 && e == GFARM_ERR_NO_SUCH_OBJECT) {
			if (gfarmfs_debug >= 2) {
				printf("UNLINK: for symlink: %s\n", path);
			}
			e = add_gfarm_prefix_symlink_suffix(path, &url);
			if (e != NULL) goto end;
			e = gfs_unlink(url);
			free(url);
		}
#endif
	} else {  /* remove a self architecture file */
		struct gfs_stat gs;

		e = add_gfarm_prefix(path, &url);
		if (e != NULL) goto end;
		e = gfs_stat(url, &gs);
#ifdef SYMLINK_MODE
		if (enable_symlink == 1 && e == GFARM_ERR_NO_SUCH_OBJECT) {
			free(url);
			if (gfarmfs_debug >= 2) {
				printf("UNLINK: for symlink: %s\n", path);
			}
			e = add_gfarm_prefix_symlink_suffix(path, &url);
			if (e != NULL) goto end;
			e = gfs_stat(url, &gs);
		}
#endif
		if (e == GFARM_ERR_NO_FRAGMENT_INFORMATION) {
			e = gfs_unlink(url);
			if (e == GFARM_ERR_NO_SUCH_OBJECT)
				e = NULL;
		} else if (e == NULL) {    /* gfs_stat succeeds */
			if (GFARM_S_IS_PROGRAM(gs.st_mode)) {
				/* executable file */
				char *arch;
				e = gfarm_host_get_self_architecture(&arch);
				if (e != NULL) {
					e = GFARM_ERR_OPERATION_NOT_PERMITTED;
				} else {
					e = gfs_unlink_section(url, arch);
				}
			} else {
				/* regular file */
				e = gfs_unlink(url);
			}
			gfs_stat_free(&gs);
		}
		free(url);
	}
end:
	return gfarmfs_final("UNLINK", e, 0, path);
}

static int
gfarmfs_rmdir(const char *path)
{
	char *e;
	char *url;

	gfarmfs_fastcreate_check();
	e = gfarmfs_init();
	if (e == NULL) {
		e = add_gfarm_prefix(path, &url);
		if (e == NULL) {
			e = gfs_rmdir(url);
			free(url);
		}
	}

	return gfarmfs_final("RMDIR", e, 0, path);
}

static int
gfarmfs_readlink(const char *path, char *buf, size_t size)
{
#ifdef SYMLINK_MODE
	/* This is for exclusive use of GfarmFS-FUSE. */
	char *e;
	char *url;
	GFS_File gf;
	int n = 0;

	gfarmfs_fastcreate_check();
	if ((e = gfarmfs_init()) != NULL) goto end;
	if (enable_symlink == 0) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		goto end;
	}
	e = add_gfarm_prefix_symlink_suffix(path, &url);
	if (e != NULL) goto end;
	e = gfs_pio_open(url, GFARM_FILE_RDONLY, &gf);
	if (e == NULL) {
		e = gfs_pio_read(gf, buf, size - 1, &n);
		gfs_pio_close(gf);
	}
	free(url);

	buf[n] = '\0';
end:
	return gfarmfs_final("READLINK", e, 0, path);
#else
	char *e;

	gfarmfs_fastcreate_check();
	e = gfarmfs_init();
	if (e == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	}
	return gfarmfs_final("READLINK", e, 0, path);
#endif
}

static int
gfarmfs_symlink(const char *from, const char *to)
{
#ifdef SYMLINK_MODE
	/* This is for exclusive use of GfarmFS-FUSE. */
	char *e;
	char *url;
	GFS_File gf;
	int n, len;

	gfarmfs_fastcreate_check();
	if ((e = gfarmfs_init()) != NULL) goto end;

	if (enable_symlink == 0) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		goto end;
	}
	e = add_gfarm_prefix_symlink_suffix(to, &url);
	if (e != NULL) goto end;
	e = gfs_pio_create(url,
			   GFARM_FILE_WRONLY|GFARM_FILE_TRUNC|
			   GFARM_FILE_EXCLUSIVE,
			   0644, &gf);
	if (e != NULL) goto free_url;
	len = strlen(from);
	e = gfs_pio_write(gf, from, len, &n);
	gfs_pio_close(gf);
	if (len != n) {
		e = gfs_unlink(url);
		e = GFARM_ERR_INPUT_OUTPUT;
		goto free_url;
	}
free_url:
	free(url);
end:
	return gfarmfs_final("SYMLINK", e, 0, to);
#else
	char *e;

	gfarmfs_fastcreate_check();
	e = gfarmfs_init();
	if (e == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	}
	return gfarmfs_final("SYMLINK", e, 0, to);
#endif
}

static int
gfarmfs_rename(const char *from, const char *to)
{
	char *e;
	char *from_url;
	char *to_url;

	gfarmfs_fastcreate_check();
	e = gfarmfs_init();
	if (e == NULL) {
		e = add_gfarm_prefix(from, &from_url);
		if (e != NULL) goto end;
		e = add_gfarm_prefix(to, &to_url);
		if (e != NULL) goto free_from_url;
		e = gfs_rename(from_url, to_url);
#ifdef SYMLINK_MODE
		if (enable_symlink == 1 && e == GFARM_ERR_NO_SUCH_OBJECT) {
			free(from_url);
			free(to_url);
			e = add_gfarm_prefix_symlink_suffix(from, &from_url);
			if (e != NULL) goto end;
			e = add_gfarm_prefix_symlink_suffix(to, &to_url);
			if (e != NULL) goto free_from_url;
			if (gfarmfs_debug >= 2) {
				printf("RENAME: for symlink: %s\n", from);
			}
			e = gfs_rename(from_url, to_url);
		}
#endif
		free(to_url);
	free_from_url:
		free(from_url);
	}
end:
	return gfarmfs_final("RENAME", e, 0, from);
}

#define COPY_BUFSIZE GFS_BLKSIZE

static int
gfarmfs_link(const char *from, const char *to)
{
	char *e, *e2;
	char *from_url, *to_url;
	GFS_File from_gf, to_gf;
	struct gfs_stat gs;
	int m, n;
	char buf[COPY_BUFSIZE];
	struct gfarm_timespec gt[2];
#ifdef SYMLINK_MODE
	int symlinkmode = 0;
#endif
	int change_mode = 0;
	mode_t save_mode = 0;

	gfarmfs_fastcreate_check();
	if ((e = gfarmfs_init()) != NULL) goto end;

	if (enable_linkiscopy == 0) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		goto end;
	}
	if (gfarmfs_debug >= 2) {
		printf("LINK: hard link is replaced by copy: %s\n", to);
	}

	/* get gfs_stat and check symlink mode */
	e = add_gfarm_prefix(from, &from_url);
	if (e != NULL) goto end;
	/* need to feee from_url [1] */
	e = gfs_stat(from_url, &gs);
#ifdef SYMLINK_MODE
	if (enable_symlink == 1 && e == GFARM_ERR_NO_SUCH_OBJECT) {
		free(from_url);
		e = add_gfarm_prefix_symlink_suffix(from, &from_url);
		if (e != NULL) goto end;
		if (gfarmfs_debug >= 2) {
			printf("LINK: for symlink: %s\n", from);
		}
		e = gfs_stat(from_url, &gs);
		symlinkmode = 1;
	}
#endif
	if (e != NULL) goto free_from_url;
	/* need to free gfs_stat [2] */

	if ((gs.st_mode & 0400) == 0) {
		save_mode = gs.st_mode;
		e = gfs_chmod(from_url, gs.st_mode|0400);
		if (e == NULL) {
			change_mode = 1;
		}
	}
	/* need to revert mode [3] */

	e = gfs_pio_open(from_url, GFARM_FILE_RDONLY, &from_gf);
	if (e != NULL) goto free_gfs_stat;
	/* need to close from_gf [4] */

	e = gfarmfs_set_view_using_url(from_gf, from_url);
	if (e != NULL) goto close_from_gf;

#ifdef SYMLINK_MODE
	if (symlinkmode == 1) {
		e = add_gfarm_prefix_symlink_suffix(to, &to_url);
	} else {
		e = add_gfarm_prefix(to, &to_url);
	}
#else
	e = add_gfarm_prefix(to, &to_url);
#endif
	if (e != NULL) goto close_from_gf;
	/* need to free to_url [5] */

	e = gfs_pio_create(to_url,
			   GFARM_FILE_WRONLY|GFARM_FILE_TRUNC|
			   GFARM_FILE_EXCLUSIVE,
			   gs.st_mode, &to_gf);
	if (e != NULL) goto free_to_url;
	/* need to close to_gf [6] */

	e = gfarmfs_set_view_using_mode(to_gf, gs.st_mode);
	if (e != NULL) goto close_to_gf;

	for (;;) { /* copy */
		e = gfs_pio_read(from_gf, buf, COPY_BUFSIZE, &m);
		if (e != NULL) break;
		if (m == 0) {
			/* EOF: success (e == NULL) */
			break;
		}
		e = gfs_pio_write(to_gf, buf, m, &n);
		if (e != NULL) break;
		if (m != n) {
			e = GFARM_ERR_INPUT_OUTPUT;
			break;
		}
	}
close_to_gf: /* [6] */
	e2 = gfs_pio_close(to_gf);
	if (e == NULL && e2 == NULL) {
		gt[0].tv_sec = gs.st_atimespec.tv_sec;
		gt[0].tv_nsec= gs.st_atimespec.tv_nsec;
		gt[1].tv_sec = gs.st_mtimespec.tv_sec;
		gt[1].tv_nsec= gs.st_mtimespec.tv_nsec;
		e = gfs_utimes(to_url, gt);
	} else {
		(void)gfs_unlink(to_url);
		if (e == NULL && e2 != NULL) {
			e = e2;
		}
	}
free_to_url: /* [5] */
	free(to_url);
close_from_gf: /* [4] */
	gfs_pio_close(from_gf);
free_gfs_stat: /* [3] */
	gfs_stat_free(&gs);
/* revert_mode:  [2] */
	if (change_mode == 1) {
		e2 = gfs_chmod(from_url, save_mode);
		if (e == NULL && e2 != NULL) {
			e = e2;
		}
	}
free_from_url: /* [1] */
	free(from_url);
end:
	return gfarmfs_final("LINK", e, 0, to);
}

static int
gfarmfs_chmod(const char *path, mode_t mode)
{
	char *e;
	char *url;

	gfarmfs_fastcreate_check();
	e = gfarmfs_init();
	if (e == NULL) {
		e = add_gfarm_prefix(path, &url);
		if (e == NULL) {
			e = gfs_chmod(url, mode);
			free(url);
		}
	}

	return gfarmfs_final("CHMOD", e, 0, path);
}

static int
gfarmfs_chown(const char *path, uid_t uid, gid_t gid)
{
	char *e;
	char *url = NULL;
	struct gfs_stat s;

	gfarmfs_fastcreate_check();
	if ((e = gfarmfs_init()) != NULL) goto end;
	e = add_gfarm_prefix(path, &url);
	if (e == NULL) {
		e = gfs_stat(url, &s);
		printf("%s: %s\n", url, e);
#ifdef SYMLINK_MODE
		if (enable_symlink == 1 &&
			 e == GFARM_ERR_NO_SUCH_OBJECT) {
			free(url);
			e = add_gfarm_prefix_symlink_suffix(path, &url);
			if (gfarmfs_debug >= 2) {
				printf("CHOWN: for symlink: %s\n", gfarm_url2path(url));
			}
			if (e == NULL)
				e = gfs_stat(url, &s);
		}
#endif
		if (e == NULL) {
			if (strcmp(s.st_user, gfarm_get_global_username())
			    != 0) {
				/* EPERM */
				e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			} /* XXX - else: do nothing */
			gfs_stat_free(&s);
		}
		free(url);
	}
end:
	return gfarmfs_final("CHOWN", e, 0, path);
}

static char *
gfarmfs_truncate_common(char *url, off_t size)
{
	char *e;
	GFS_File gf;

	e = gfs_pio_open(url, GFARM_FILE_WRONLY, &gf);
	if (e != NULL)
		return (e);
	e = gfarmfs_set_view_using_url(gf, url);
	if (e == NULL) {
		e = gfs_pio_truncate(gf, size);
	}
	gfs_pio_close(gf);
	return (e);
}

static int
gfarmfs_truncate(const char *path, off_t size)
{
	char *e;
	char *url;

	gfarmfs_fastcreate_check();
	e = gfarmfs_init();
	if (e != NULL)
		goto end;
	e = add_gfarm_prefix(path, &url);
	if (e == NULL) {
		e = gfarmfs_truncate_common(url, size);
		free(url);
	}
end:
	return gfarmfs_final("TRUNCATE", e, 0, path);
}

static int
gfarmfs_utime(const char *path, struct utimbuf *buf)
{
	char *e;
	char *url;

	gfarmfs_fastcreate_check();
	e = add_gfarm_prefix(path, &url);
	if (e != NULL) goto end;
	e = gfarmfs_init();
	if (e == NULL) {
		if (buf == NULL)
			e = gfs_utimes(url, NULL);
		else {
			struct gfarm_timespec gt[2];

			gt[0].tv_sec = buf->actime;
			gt[0].tv_nsec= 0;
			gt[1].tv_sec = buf->modtime;
			gt[1].tv_nsec= 0;
			e = gfs_utimes(url, gt);
		}
	}
	free(url);
end:
	return gfarmfs_final("UTIME", e, 0, path);
}

static inline GFS_File
gfarmfs_cast_fh(struct fuse_file_info *fi)
{
	return (GFS_File)(unsigned long) fi->fh;
}

static int
gfarmfs_open(const char *path, struct fuse_file_info *fi)
{
	char *e;
	char *url;
	int flags = 0;
	GFS_File gf;

	e = gfarmfs_init();
	if (e != NULL)
		goto end;
	e = add_gfarm_prefix(path, &url);
	if (e != NULL)
		goto end;
	if ((fi->flags & O_ACCMODE) == O_RDONLY) {
		flags = GFARM_FILE_RDONLY;
	} else if ((fi->flags & O_ACCMODE) == O_WRONLY) {
		flags = GFARM_FILE_WRONLY;
	} else if ((fi->flags & O_ACCMODE) == O_RDWR) {
		flags = GFARM_FILE_RDWR;
	}
	if (enable_gfarm_iobuf == 0) {
		flags |= GFARM_FILE_UNBUFFERED;
	}
#ifdef ENABLE_FASTCREATE
	if (enable_fastcreate > 0) {
		/* check a created file on memory and create/open */
		/* with checking program */
		e = gfarmfs_fastcreate_open(url, flags, &gf);
		if (e != NULL)
			goto free_url;
	} else {
#endif
		e = gfs_pio_open(url, flags, &gf);
		if (e != NULL)
			goto free_url;
		e = gfarmfs_set_view_using_url(gf, url);
		if (e != NULL) {
			gfs_pio_close(gf);
			goto free_url;
		}
#ifdef ENABLE_FASTCREATE
	}
#endif
	fi->fh = (unsigned long) gf;
free_url:
	free(url);
end:
	return gfarmfs_final("OPEN", e, 0, path);
}

#if FUSE_USE_VERSION >= 25
static int
gfarmfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	char *e;
	char *url;
	int flags = 0;
	GFS_File gf;

	e = gfarmfs_init();
	while (e == NULL) {
		if ((fi->flags & O_ACCMODE) == O_RDONLY) {
			flags = GFARM_FILE_RDONLY;
		} else if ((fi->flags & O_ACCMODE) == O_WRONLY) {
			flags = GFARM_FILE_WRONLY;
		} else if ((fi->flags & O_ACCMODE) == O_RDWR) {
			flags = GFARM_FILE_RDWR;
		}
		if (enable_gfarm_iobuf == 0) {
			flags |= GFARM_FILE_UNBUFFERED;
		}

		e = add_gfarm_prefix(path, &url);
		if (e != NULL) break;
		e = gfs_pio_create(url,
				   flags|GFARM_FILE_TRUNC|GFARM_FILE_EXCLUSIVE,
				   mode, &gf);
		if (e != NULL) {
			free(url);
			break;
		}
		e = gfarmfs_set_view_using_mode(gf, mode);
		free(url);
		if (e != NULL) {
			gfs_pio_close(gf);
			break;
		}
		fi->fh = (unsigned long) gf;
		break;
	}

	return gfarmfs_final("CREATE", e, 0, path);
}

static int
gfarmfs_fgetattr(const char *path, struct stat *stbuf,
		 struct fuse_file_info *fi)
{
	char *e;
	struct gfs_stat gs;

	e = gfarmfs_init();
	if (e == NULL) {
		e = gfs_fstat(gfarmfs_cast_fh(fi), &gs);
		if (e == NULL) {
			e = convert_gfs_stat_to_stat(NULL, &gs, stbuf, 0);
			gfs_stat_free(&gs);
		}
	}

	return gfarmfs_final("FGETATTR", e, 0, path);
}

static int
gfarmfs_ftruncate(const char *path, off_t size, struct fuse_file_info *fi)
{
	char *e;
	GFS_File gf;

	e = gfarmfs_init();
	if (e == NULL) {
		gf = gfarmfs_cast_fh(fi);
		e = gfs_pio_truncate(gf, size);
	}

	return gfarmfs_final("FTRUNCATE", e, 0, path);
}

static int
gfarmfs_access(const char *path, int mask)
{
	char *e;
	char *url;

	e = gfarmfs_init();
	if (e == NULL) {
		e = add_gfarm_prefix(path, &url);
		if (e == NULL) {
			e = gfs_access(url, mask);
			free(url);
		}
	}

	return gfarmfs_final("ACCESS", e, 0, path);
}
#endif /* FUSE_USE_VERSION >= 25 */

static int
gfarmfs_release(const char *path, struct fuse_file_info *fi)
{
	char *e;

	gfarmfs_fastcreate_check();
	e = gfarmfs_init();
	if (e == NULL) {
		e = gfs_pio_close(gfarmfs_cast_fh(fi));
	}

	return gfarmfs_final(OP_RELEASE, e, 0, path);
}

static int
gfarmfs_read(const char *path, char *buf, size_t size, off_t offset,
	     struct fuse_file_info *fi)
{
	int n;
	file_offset_t off;
	char *e;
	GFS_File gf;

	e = gfarmfs_init();
	while (e == NULL) {
		gf = gfarmfs_cast_fh(fi);
		e = gfs_pio_seek(gf, offset, 0, &off);
		if (e != NULL) break;
		e = gfs_pio_read(gf, buf, size, &n);
		break;
	}

	return gfarmfs_final("READ", e, n, path);
}

static int
gfarmfs_write(const char *path, const char *buf, size_t size,
	      off_t offset, struct fuse_file_info *fi)
{
	int n;
	file_offset_t off;
	char *e;
	GFS_File gf;

	e = gfarmfs_init();
	while (e == NULL) {
		gf = gfarmfs_cast_fh(fi);
		e = gfs_pio_seek(gf, offset, 0, &off);
		if (e != NULL) break;
		e = gfs_pio_write(gf, buf, size, &n);
		break;
	}

	return gfarmfs_final("WRITE", e, n, path);
}

static int
gfarmfs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
	char *e;
	GFS_File gf;

	if ((e = gfarmfs_init()) != NULL) goto end;

	gf = gfarmfs_cast_fh(fi);
	if (isdatasync) {
		e = gfs_pio_datasync(gf);
	} else {
		e = gfs_pio_sync(gf);
	}
end:
	return gfarmfs_final("FSYNC", e, 0, path);
}

#ifdef USE_GFS_STATFSNODE
static char *
gfs_statfsnode_cached_and_retry(
	char *hostname,
	gfarm_int32_t *bsizep,
	file_offset_t *blocksp, file_offset_t *bfreep, file_offset_t *bavailp,
	file_offset_t *filesp, file_offset_t *ffreep, file_offset_t *favailp)
{
	char *e;

	e = gfs_statfsnode_cached(hostname,
				  bsizep, blocksp, bfreep, bavailp,
				  filesp, ffreep, favailp);
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		e = gfs_statfsnode(hostname,
				   bsizep, blocksp, bfreep, bavailp,
				   filesp, ffreep, favailp);
	}
	if (e != NULL) {
		gfarmfs_errlog("STATFS: %s: %s", hostname, e);
		if (e == GFARM_ERR_NO_SUCH_OBJECT) {
			e = GFARM_ERR_CONNECTION_REFUSED;
		}
	}

	return (e);
}

static char *
gfs_statfsnode_total_of_hostlist(
	char **hosts, int nhosts,
	gfarm_int32_t *bsizep,
	file_offset_t *blocksp, file_offset_t *bfreep, file_offset_t *bavailp,
	file_offset_t *filesp, file_offset_t *ffreep, file_offset_t *favailp)
{
	char *e, *e_save = NULL;
	int i, ok;
	gfarm_int32_t bsize;
	file_offset_t blocks, bfree ,bavail, files, ffree, favail;

	*bsizep = *blocksp = *bfreep = *bavailp
		= *filesp = *ffreep = *favailp = 0;
	ok = 0;
	for (i = 0; i < nhosts; i++) {
		e = gfs_statfsnode_cached_and_retry(hosts[i], &bsize,
						    &blocks, &bfree,
						    &bavail, &files,
						    &ffree, &favail);
		if (e == NULL) {
			if (*bsizep == 0) {
				if (bsize == 0)
					continue;
				*bsizep = bsize;
			}
			ok = 1;
			if (*bsizep == bsize) {
				*blocksp += blocks;
				*bfreep  += bfree;
				*bavailp += bavail;
			} else { /* revision (rough) */
				*blocksp += blocks * bsize / *bsizep;
				*bfreep  += bfree * bsize / *bsizep;
				*bavailp += bavail * bsize / *bsizep;
			}
			*filesp  += files;
			*ffreep  += ffree;
			*favailp += favail;
		} else {
			e_save = e;
		}
	}
	if (ok == 1) {
		return (NULL);
	} else {
		return (e_save);
	}
}

static inline int
gfarmfs_statfs_common(
	const char *path, gfarm_int32_t *bsizep,
	file_offset_t *blocksp, file_offset_t *bfreep, file_offset_t *bavailp,
	file_offset_t *filesp, file_offset_t *ffreep, file_offset_t *favailp)
{
	char *e;
	struct gfarm_host_info *hostinfos;
	int nhosts, i;
	char **hosts;

	if ((e = gfarmfs_init()) != NULL)
		goto end;
	e = gfarm_host_info_get_all(&nhosts, &hostinfos);
	if (e != NULL)
		goto end;
	gfarm_host_info_free_all(nhosts, hostinfos);
	hosts = malloc(nhosts * sizeof(char *));
	if (hosts == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto end;
	}
	e = gfarm_schedule_search_idle_acyclic_by_domainname(
		"", &nhosts, hosts);
	if (e != NULL)
		goto free_hosts;
	e = gfs_statfsnode_total_of_hostlist(
		hosts, nhosts, bsizep, blocksp,
		bfreep, bavailp, filesp, ffreep, favailp);
	for (i = 0; i < nhosts; i++)
		if (hosts[i])
			free(hosts[i]);
free_hosts:
	free(hosts);
end:
	return gfarmfs_final("STATFS", e, 0, path);
}

#if FUSE_USE_VERSION == 22
static int
gfarmfs_statfs(const char *path, struct statfs *stfs)
{
	file_offset_t favail;
	return gfarmfs_statfs_common(path,
				     (gfarm_int32_t*)&stfs->f_bsize,
				     (file_offset_t*)&stfs->f_blocks,
				     (file_offset_t*)&stfs->f_bfree,
				     (file_offset_t*)&stfs->f_bavail,
				     (file_offset_t*)&stfs->f_files,
				     (file_offset_t*)&stfs->f_ffree,
				     (file_offset_t*)&favail);
}
#elif FUSE_USE_VERSION >= 25
static int
gfarmfs_statfs(const char *path, struct statvfs *stvfs)
{
	int res;
	gfarm_int32_t bsize = 0;
	res = gfarmfs_statfs_common(path, &bsize,
				    (file_offset_t*)&stvfs->f_blocks,
				    (file_offset_t*)&stvfs->f_bfree,
				    (file_offset_t*)&stvfs->f_bavail,
				    (file_offset_t*)&stvfs->f_files,
				    (file_offset_t*)&stvfs->f_ffree,
				    (file_offset_t*)&stvfs->f_favail);
	stvfs->f_bsize = (unsigned long) bsize;
	return (res);
}
#endif /* FUSE_USE_VERSION */
#endif /* USE_GFS_STATFSNODE */

#if 1
static int
gfarmfs_flush(const char *path, struct fuse_file_info *fi)
{
	char *e = gfarmfs_init();
#if 0 /* XXX TODO */
	GFS_File gf;
	if (e != NULL) goto end;
	if ((fi->flags & O_ACCMODE) != O_RDONLY) {
		gf = gfarmfs_cast_fh(fi);
		e = gfs_pio_sync(gf);
	}
end:
#endif
	return gfarmfs_final("(FLUSH)", e, 0, path);
}
#endif

#if ENABLE_XATTR
static int
gfarmfs_setxattr(const char *path, const char *name, const char *value,
		 size_t size, int flags)
{
	return (-ENOSYS);
}

static int
gfarmfs_getxattr(const char *path, const char *name, char *value,
		 size_t size)
{
	return (-ENOSYS);
}

static int
gfarmfs_listxattr(const char *path, char *list, size_t size)
{
	return (-ENOSYS);
}

static int
gfarmfs_removexattr(const char *path, const char *name)
{
	return (-ENOSYS);
}
#endif /* ENABLE_XATTR */

static struct fuse_operations gfarmfs_oper_base = {
	.getattr   = gfarmfs_getattr,
	.readlink  = gfarmfs_readlink,
	.getdir    = gfarmfs_getdir,
	.mknod     = gfarmfs_mknod,
	.mkdir     = gfarmfs_mkdir,
	.symlink   = gfarmfs_symlink,
	.unlink    = gfarmfs_unlink,
	.rmdir     = gfarmfs_rmdir,
	.rename    = gfarmfs_rename,
	.link      = gfarmfs_link,
	.chmod     = gfarmfs_chmod,
	.chown     = gfarmfs_chown,
	.truncate  = gfarmfs_truncate,
	.utime     = gfarmfs_utime,
	.open      = gfarmfs_open,
	.read      = gfarmfs_read,
	.write     = gfarmfs_write,
	.release   = gfarmfs_release,
	.fsync     = gfarmfs_fsync,
#ifdef USE_GFS_STATFSNODE
	.statfs    = gfarmfs_statfs,
#endif
#if FUSE_USE_VERSION >= 25
	.create    = gfarmfs_create,
	.fgetattr  = gfarmfs_fgetattr,
	.ftruncate = gfarmfs_ftruncate,
	.access    = gfarmfs_access,
#endif
	.flush     = gfarmfs_flush,
#if ENABLE_XATTR
	.setxattr    = gfarmfs_setxattr,
	.getxattr    = gfarmfs_getxattr,
	.listxattr   = gfarmfs_listxattr,
	.removexattr = gfarmfs_removexattr,
#endif
};

/* ################################################################### */
/* new I/O functions (share GFS_File during opening the same file) */

#define FH_LIST_USE_INO 0  /* key: 0=path, 1=ino */

#if ENABLE_ASYNC_REPLICATION
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define DEFAULT_FORK_MAX (5)
#endif

struct gfarmfs_fh {
#if FH_LIST_USE_INO == 1
	long ino;
#else
	char *path;
#endif
	GFS_File gf;
	int flags; /* current flag */
	int nopen;
	int nwrite;
#if REVISE_CHMOD
	int mode_changed;
	mode_t save_mode;
#endif
#if REVISE_UTIME
	int utime_changed;
	struct gfarm_timespec save_utime[2];
#endif
#if ENABLE_ASYNC_REPLICATION
	int *shm_child_status;
	pid_t child_pid;
#endif
};

typedef struct gfarmfs_fh * FH;

/* async replication */
#if ENABLE_ASYNC_REPLICATION
#define CHILD_RUNNING  0
#define CHILD_CANCELED 1
#define CHILD_DONE     2

#if 0
/* for child */
static int
gfarmfs_async_is_canceled(FH fh)
{
	if (fh->shm_child_status != NULL &&
	    *fh->shm_child_status != CHILD_RUNNING)
		return (1);
	else
		return (0);
}
#endif

/* for parent */
static int
gfarmfs_async_is_running(FH fh)
{
	if (fh->shm_child_status != NULL &&
	    *fh->shm_child_status == CHILD_RUNNING)
		return (1);
	else
		return (0);
}

#if 0
/* for parent */
static int
gfarmfs_async_is_done(FH fh)
{
	if (fh->shm_child_status == NULL) /* no forking */
		return (1);
	else if (*fh->shm_child_status == CHILD_DONE)
		return (1);
	else
		return (0);
}
#endif

#if 0
static void
gfarmfs_async_start(FH fh)
{
	if (fh != NULL && fh->shm_child_status != NULL)
		*fh->shm_child_status = CHILD_RUNNING;
}
#endif

/* cancel is not supported yet */
static void
gfarmfs_async_stop(FH fh)
{
	if (fh != NULL && fh->shm_child_status != NULL)
		*fh->shm_child_status = CHILD_CANCELED;
}

static void
gfarmfs_async_wait(FH fh)
{
	if (fh != NULL && fh->child_pid > 0) {
		waitpid(fh->child_pid, NULL, 0);
		fh->child_pid = -1;
		if (fh->shm_child_status != NULL)
			*fh->shm_child_status = CHILD_DONE;
	}
}

static int
gfarmfs_fh_is_opened(FH fh)
{
	if (fh != NULL && fh->nopen > 0)
		return (1);
	else
		return (0);
}
#endif

#define FH_LIST_LEN 1024

FH gfarmfs_fh_list[FH_LIST_LEN];

#if FH_LIST_USE_INO == 1
#define FH_GET1(url)          gfarmfs_fh_get_key_url(url)
#define FH_GET2(url, ino)     gfarmfs_fh_get(ino)
#define FH_ADD1(url, fh)      gfarmfs_fh_add_key_url(url, fh)
#define FH_ADD2(url, ino, fh) gfarmfs_fh_add(ino, fh)
#define FH_REMOVE1(url)       gfarmfs_fh_remove_key_url(url)
#define FH_REMOVE2(url, ino)  gfarmfs_fh_remove(ino)
#define FH_FREE(fh)           gfarmfs_fh_free(fh)
#else
#define FH_GET1(url)          gfarmfs_fh_get(url)
#define FH_GET2(url, ino)     gfarmfs_fh_get(url)
#define FH_ADD1(url, fh)      gfarmfs_fh_add(url, fh)
#define FH_ADD2(url, ino, fh) gfarmfs_fh_add(url, fh)
#define FH_REMOVE1(url)       gfarmfs_fh_remove(url)
#define FH_REMOVE2(url, ino)  gfarmfs_fh_remove(url)
#define FH_FREE(fh)           gfarmfs_fh_free(fh)
#endif

static void
gfarmfs_fh_free(FH fh)
{
#if ENABLE_ASYNC_REPLICATION
	if (fh->child_pid > 0)
		waitpid(fh->child_pid, NULL, 0);
	if (fh->shm_child_status)
		shmdt(fh->shm_child_status);
#endif
#if FH_LIST_USE_INO == 0
	if (fh->path)
		free(fh->path);
#endif
	free(fh);
}

#if FH_LIST_USE_INO == 1
/* key is ino */
static char *
gfarmfs_get_ino(char *url, long *inop)
{
	char *e;
	struct gfs_stat gs;

	e = gfs_stat(url, &gs);
	if (e == NULL) {
		*inop = gs.st_ino;
		gfs_stat_free(&gs);
		return (NULL);
	}
	return (e);
}

static FH
gfarmfs_fh_get(long ino)
{
	int i;

	for (i = 0; i < FH_LIST_LEN; i++) {
		if (gfarmfs_fh_list[i] != NULL &&
		    gfarmfs_fh_list[i]->ino == ino) {
			return gfarmfs_fh_list[i];
		}
	}
	return (NULL);
}

static FH
gfarmfs_fh_get_key_url(char *url)
{
	char *e;
	long ino;

	e = gfarmfs_get_ino(url, &ino);
	if (e != NULL)
		return (NULL);
	return gfarmfs_fh_get(ino);
}

/* must do gfarmfs_fh_remove before this */
static char *
gfarmfs_fh_add(long ino, FH fh)
{
	int i;

#if 0 /* for debug */
	if (gfarmfs_fh_get(ino))
		return (GFARM_ERR_ALREADY_EXISTS);
#endif
	for (i = 0; i < FH_LIST_LEN; i++) {
		if (gfarmfs_fh_list[i] == NULL) {
			fh->ino = ino;
			gfarmfs_fh_list[i] = fh;
			return (NULL);
		}
#if ENABLE_ASYNC_REPLICATION
		else if (enable_replication != REP_DISABLE &&
			 gfarmfs_fh_list[i]->nopen <= 0 &&
			 !gfarmfs_async_is_running(gfarmfs_fh_list[i])) {
			FH_FREE(gfarmfs_fh_list[i]);
			fh->ino = ino;
			gfarmfs_fh_list[i] = fh;
			return (NULL);
		}
#endif
	}
	return (GFARM_ERR_NO_MEMORY); /* EMFILE ? */
}

#if 0
static char *
gfarmfs_fh_add_key_url(char *url, FH fh)
{
	char *e;
	long ino;

	e = gfarmfs_get_ino(url, &ino);
	if (e != NULL)
		return (e);
	return gfarmfs_fh_add(ino, fh);
}
#endif

static void
gfarmfs_fh_remove(long ino)
{
	int i;

	for (i = 0; i < FH_LIST_LEN; i++) {
		if (gfarmfs_fh_list[i] != NULL &&
		    gfarmfs_fh_list[i]->ino == ino) {
			gfarmfs_fh_list[i] = NULL;
			return;
		}
	}
	return;
}

#if 0
static void
gfarmfs_fh_remove_key_url(char *url)
{
	char *e;
	long ino;
	e = gfarmfs_get_ino(url, &ino);
	if (e != NULL)
		return;
	return gfarmfs_fh_remove(ino);
}
#endif

#else /* FH_LIST_USE_INO != 0 */
/* key is url */

static FH
gfarmfs_fh_get(char *url)
{
	int i;
	char *path;

	if (url == NULL)
		return (NULL);
	path = gfarm_url2path(url);
	for (i = 0; i < FH_LIST_LEN; i++) {
		if (gfarmfs_fh_list[i] != NULL &&
		    strcmp(gfarmfs_fh_list[i]->path, path) == 0) {
			return gfarmfs_fh_list[i];
		}
	}
	return (NULL);
}

/* must do gfarmfs_fh_remove before this */
static char *
gfarmfs_fh_add(char *url, FH fh)
{
	int i;
	char *path;

	if (url == NULL)
		return (NULL);
#if 0 /* for debug */
	if (gfarmfs_fh_get(url))
		return (GFARM_ERR_ALREADY_EXISTS);
#endif
	path = gfarm_url2path(url);
	for (i = 0; i < FH_LIST_LEN; i++) {
		if (gfarmfs_fh_list[i] == NULL) {
			if (fh->path != NULL)
				free(fh->path);
			fh->path = strdup(path);
			if (fh->path == NULL)
				return (GFARM_ERR_NO_MEMORY);
			gfarmfs_fh_list[i] = fh;
			return (NULL);
		}
#if ENABLE_ASYNC_REPLICATION
		else if (enable_replication != REP_DISABLE &&
			 gfarmfs_fh_list[i]->nopen <= 0 &&
			 !gfarmfs_async_is_running(gfarmfs_fh_list[i])) {
			FH_FREE(gfarmfs_fh_list[i]);
			if (fh->path != NULL)
				free(fh->path);
			fh->path = strdup(path);
			if (fh->path == NULL)
				return (GFARM_ERR_NO_MEMORY);
			gfarmfs_fh_list[i] = fh;
			return (NULL);
		}
#endif
	}
	return (GFARM_ERR_NO_MEMORY); /* EMFILE ? */
}

static void
gfarmfs_fh_remove(char *url)
{
	int i;
	char *path;

	if (url == NULL)
		return;
	path = gfarm_url2path(url);
	for (i = 0; i < FH_LIST_LEN; i++) {
		if (gfarmfs_fh_list[i] != NULL &&
		    strcmp(gfarmfs_fh_list[i]->path, path) == 0) {
			gfarmfs_fh_list[i] = NULL;
			return;
		}
	}
	return;
}

#endif /* FH_LIST_USE_INO == 1 */

#if ENABLE_ASYNC_REPLICATION
static void *
gfarmfs_shm_alloc(size_t size)
{
	int shmid;
	void *b;
	int save_errno;

	shmid = shmget(IPC_PRIVATE, size, 0600|IPC_CREAT);
	if (shmid == -1)
		return (NULL);
	b = shmat(shmid, NULL, 0);
	if (b == (void *) -1)
		return (NULL);
	if (shmctl(shmid, IPC_RMID, 0) == -1) {
		save_errno = errno;
		shmdt(b);
		errno = save_errno;
		return (NULL);
	}
	return (b);
}

#define USE_SEMAPHORE 1

#if USE_SEMAPHORE
#include <sys/sem.h>

static int semid;
static int sem_initialized = 0;

static char *
gfarmfs_async_fork_count_initialize()
{
	int save_errno;
	struct sembuf sb = {0, DEFAULT_FORK_MAX, 0};
	if (sem_initialized == 1)
		return (NULL);
	sem_initialized = 1;
	if ((semid = semget(IPC_PRIVATE, 1, 0600|IPC_CREAT)) == -1) {
		save_errno = errno;
		perror("semget");
		return (gfarm_errno_to_error(save_errno));
	}
	if (semop(semid, &sb, 1) == -1) {
		save_errno = errno;
		perror("semop");
		return (gfarm_errno_to_error(save_errno));
	}
	return (NULL);
}

static char *
gfarmfs_async_fork_count_increment_n(int n)
{
	int save_errno;
	struct sembuf sb = {0, -n, 0};
	if (semop(semid, &sb, 1) == -1) {
		save_errno = errno;
		perror("semop");
		return gfarm_errno_to_error(save_errno);
	}
	/* printf("semval = %d\n", semctl(semid, 0, GETVAL, 0)); */
	return (NULL);
}

static char *
gfarmfs_async_fork_count_decrement_n(int n)
{
	int save_errno;
	struct sembuf sb = {0, n, 0};
	if (semop(semid, &sb, 1) == -1) {
		save_errno = errno;
		perror("semop");
		return gfarm_errno_to_error(save_errno);
	}
	return (NULL);
}

static void
gfarmfs_async_fork_count_finalize()
{
	sem_initialized = 0;
	if (semctl(semid, 0, IPC_RMID, 0) == -1)
		perror("semctl");
}

#else  /* ! USE_SEMAPHORE */
/* not work correctly  */
static int *fork_count = NULL; /* shm (for counter) */

static char *
gfarmfs_async_fork_count_initialize()
{
	if (fork_count == NULL) {
		fork_count = gfarmfs_shm_alloc(sizeof(int));
		if (fork_count == NULL)
			return (gfarm_errno_to_error(errno));
		*fork_count = 0;
	}
	return (NULL);
}

static char *
gfarmfs_async_fork_count_increment_n(int n)
{
	*fork_count += n;
	if (*fork_count > DEFAULT_FORK_MAX)
		waitpid(-1, NULL, 0);
	return (NULL);
}

static char *
gfarmfs_async_fork_count_decrement_n(int n)
{
	*fork_count -= n;
	return (NULL);
}

#define gfarmfs_async_fork_count_finalize()
#endif /* USE_SEMAPHORE */

#define gfarmfs_async_fork_count_increment() \
	gfarmfs_async_fork_count_increment_n(1)

#define gfarmfs_async_fork_count_decrement() \
	gfarmfs_async_fork_count_decrement_n(1)

#ifdef USE_GFARM_SCRAMBLE

static void
gfarmfs_scramble_replicate(char *url)
{
	char *e;

	e = gfarm_terminate();
	if (e != NULL)
		goto emsg;
	e = gfarm_initialize(NULL, NULL);
	if (e != NULL)
		goto emsg;
	e = gfs_scramble_make_replica(url);
	if (e != NULL)
		goto emsg;
	e = gfarm_terminate();
emsg:
	if (e != NULL)
		gfarmfs_errlog("REPLICATE: %s: %s", gfarm_url2path(url), e);
}
#endif

#ifdef USE_GFREP_H
/* *nhostsp > 0 : OK
 * *nhostsp == 0: do nothing
 * *nhostsp = -1: retry (gfrep -N)
 * error: undefined
 */
static char *
gfarmfs_gfrep_H_init(char *url, int *nhostsp, char **hosts)
{
	const int nrequire = *nhostsp;
	char *e;
	int nsections, ncopies, nhostinfos;
	struct gfarm_file_section_info *sections;
	struct gfarm_file_section_copy_info *copies;
	struct gfarm_host_info *hostinfos;
	int i, nhave, ntmplist;
	char **have_hosts, **tmplist;
	char *gfarm_path;

	if (nrequire <= 0) {
		*nhostsp = 0;
		return (NULL);
	}
	e = gfarm_url_make_path(url, &gfarm_path);
	if (e != NULL)
		return (e);
	/* !!! need free_gfarm_path !!! */
	e = gfarm_file_section_info_get_all_by_file(
		gfarm_path, &nsections, &sections);
	if (e != NULL)
		goto free_gfarm_path;
	if (nsections <= 0 || nsections >= 2) {
		gfarm_file_section_info_free_all(nsections, sections);
		e = NULL;
		*nhostsp = -1; /* gfrep -N */
		goto free_gfarm_path;
	}
	/* ------ nsections == 1 ------ */
	e = gfarm_file_section_copy_info_get_all_by_section(
		gfarm_path, sections[0].section, &ncopies, &copies);
	gfarm_file_section_info_free_all(nsections, sections);
	if (e != NULL)
		goto free_gfarm_path;
	/* !!! need free_copy_info !!! */
	have_hosts = malloc(ncopies * sizeof(char *));
	if (have_hosts == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_copy_info;
	}
	/* !!! need free_have_hosts !!! */
	if (gfrep_dom != NULL) {
		int j = 0;
		for (i = 0; i < ncopies; i++) {
			if (gfarm_host_is_in_domain(copies[i].hostname,
						    gfrep_dom))
				have_hosts[j++] = copies[i].hostname;
		}
		nhave = j;
	} else {
		for (i = 0; i < ncopies; i++)
			have_hosts[i] = copies[i].hostname;
		nhave = ncopies;
	}
	if (nhave >= nrequire) {
		e = NULL;
		*nhostsp = 0;
		goto free_have_hosts;
	}
	/* ------ nrequire > nhave ------ */
	e = gfarm_host_info_get_all(&nhostinfos, &hostinfos);
	if (e != NULL) {
		goto free_have_hosts;
	}
	/* !!! need free_hostinfos !!! */
	/* length of tmplist is the same as hostinfos */
	tmplist = calloc(nhostinfos, sizeof(char *));
	if (tmplist == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_hostinfos;
	}
	/* !!! need free_tmplist !!! */
	ntmplist = 0;
	for (i = 0; i < nhostinfos; i++) {
		int ignore = 0, j;
		if (gfrep_dom != NULL &&
		    !gfarm_host_is_in_domain(
			    hostinfos[i].hostname, gfrep_dom)) {
			continue;
		}
		for (j = 0; j < nhave; j++) {
			if (strcasecmp(hostinfos[i].hostname, have_hosts[j])
			    == 0) {
				ignore = 1;
				break;
			}
		}
		if (ignore == 0) /* add */
			tmplist[ntmplist++] = hostinfos[i].hostname;
	}
	if (ntmplist == 0) {
		*nhostsp = 0;
		e = NULL;
		gfarmfs_errlog(
			"REPLICATE: WARN: "
			"require(%d) - have(%d) > selected(0)",
			nrequire, nhave);
		goto free_tmplist;
	}
	/* ntmplist > 0 */
	/* schedule the number (nrequire - nhave) of hosts from tmplist */
	*nhostsp = nrequire - nhave;
	if (*nhostsp > ntmplist)
		*nhostsp = ntmplist;
	e = gfarm_schedule_search_idle_acyclic_hosts_to_write(
		ntmplist, tmplist, nhostsp, hosts);
	if (e == NULL) {
		for (i = 0; i < *nhostsp; i++) {
			char *tmp;
			int j;
			tmp = strdup(hosts[i]);
			if (tmp == NULL) {
				for (j = 0; j < i - 1; j++)
					if (hosts[j])
						free(hosts[j]);
				e = GFARM_ERR_NO_MEMORY;
				goto free_tmplist;
			}
			hosts[i] = tmp;
		}
		if (nrequire - nhave > *nhostsp)
			gfarmfs_errlog(
				"REPLICATE: WARN: "
				"require(%d) - have(%d) > selected(%d)",
			nrequire, nhave, *nhostsp);
	}
free_tmplist:
	free(tmplist);
free_hostinfos:
	gfarm_host_info_free_all(nhostinfos, hostinfos);
free_have_hosts:
	free(have_hosts);
free_copy_info:
	gfarm_file_section_copy_info_free_all(ncopies, copies);
free_gfarm_path:
	free(gfarm_path);

	return (e);
}

static void
gfarmfs_gfrep_H_exec(char *url, int nhosts, char **hosts)
{
	int p, i;
	int pfds[2];
	FILE *f;

	if (pipe(pfds) == -1)
		gfarmfs_errlog("REPLICATE: pipe: %s", strerror(errno));
	p = fork();
	if (p == -1) {
		gfarmfs_errlog("REPLICATE: fork: %s", strerror(errno));
		return;
	} else if (p == 0) { /* grandchild */
		close(pfds[1]);
		if (dup2(pfds[0], 0) == -1) { /* stdin */
			gfarmfs_errlog("REPLICATE: dup2(grandchild): %s",
				       strerror(errno));
			_exit(0);
		}
		close(pfds[0]);
		if (execl(gfrep_path, gfrep_path, "-H", "-", url, (char*)NULL)
		    == -1)
			gfarmfs_errlog("REPLICATE: execvp: %s",
				       strerror(errno));
		_exit(1);
	}
	close(pfds[0]);
	f = fdopen(pfds[1], "w");
	if (f == NULL) {
		gfarmfs_errlog("REPLICATE: fdopen: %s", strerror(errno));
		close(pfds[1]);
		return;
	}
	for (i = 0; i < nhosts; i++) {
		if (fprintf(f, "%s\n", hosts[i]) < 0) {
			gfarmfs_errlog("REPLICATE: fprintf: %s",
				       strerror(errno));
			break;
		}
	}
	if (fclose(f) == EOF)
		gfarmfs_errlog("REPLICATE: fclose: %s", strerror(errno));
	waitpid(p, NULL, 0);
}
#endif /* end of USE_GFREP_H */

static char *gfrep_N_argv[7];

static void
gfarmfs_gfrep_N_init()
{
	static char str_N[] = "-N";
	static char str_D[] = "-D";
	static char gfrep_num[16];
	static int initialized = 0;

	if (initialized)
		return;
	snprintf(gfrep_num, 16, "%d", gfrep_target_num);
	if (gfrep_dom == NULL) {
		/* gfrep -N 2 */
		gfrep_N_argv[0] = gfrep_path;
		gfrep_N_argv[1] = str_N;
		gfrep_N_argv[2] = gfrep_num;
		/* gfrep_N_argv[3] for url */
		gfrep_N_argv[4] = NULL;
	} else {
		/* gfrep -N 2 -D example.com */
		gfrep_N_argv[0] = gfrep_path;
		gfrep_N_argv[1] = str_N;
		gfrep_N_argv[2] = gfrep_num;
		gfrep_N_argv[3] = str_D;
		gfrep_N_argv[4] = gfrep_dom;
		/* gfrep_N_argv[5] for url */
		gfrep_N_argv[6] = NULL;
	}
	initialized = 1;
}

static void
gfarmfs_gfrep_N_exec(char *url)
{
	int p;

	p = fork();
	if (p == -1) {
		gfarmfs_errlog("REPLICATE: fork: %s", strerror(errno));
		return;
	} else if (p == 0) { /* grandchild */
		if (gfrep_dom == NULL)
			gfrep_N_argv[3] = url;
		else
			gfrep_N_argv[5] = url;
		if (execv(gfrep_N_argv[0], gfrep_N_argv) == -1)
			gfarmfs_errlog("REPLICATE: execvp: %s",
				       strerror(errno));
		_exit(1);
	}
	waitpid(p, NULL, 0);
#if 0
	/* gfwhere */
	p = fork();
	if (p == 0) {
		execlp("gfwhere", "gfwhere", "-s", url, (char*)NULL);
		_exit(0);
	} else if (p > 0)
		waitpid(p, NULL, 0);
#endif
}

static char *
gfarmfs_async_replicate(char *url, FH fh)
{
	char *e;
	int rep_mode = REP_DISABLE;
#ifdef USE_GFREP_H
	char **hosts = NULL;
	int nhosts = gfrep_target_num;
#endif

	if (fh->shm_child_status == NULL) { /* initialize */
		fh->shm_child_status = gfarmfs_shm_alloc(sizeof(int));
		if (fh->shm_child_status == NULL)
			return (gfarm_errno_to_error(errno));
	} else {
		gfarmfs_async_stop(fh);  /* cancel */
		gfarmfs_async_wait(fh);
	}
	if (enable_replication == REP_DISABLE) {
		/* do nothing */
		return (NULL);
	} else if (enable_replication == REP_GFREP_N){
		gfarmfs_gfrep_N_init();
		rep_mode = REP_GFREP_N;
#ifdef USE_GFARM_SCRAMBLE
	} else if (enable_replication == REP_SCRAMBLE) {
		rep_mode = REP_SCRAMBLE;
#endif
	} else {
#ifdef USE_GFREP_H
#if FIX_GFREP_H
		/* gfrep -H cannot operate an executable file in
		 * gfarm v1.4 or earlier
		 */
		struct gfs_stat gs;
		mode_t mode;
		e = gfs_stat(url, &gs);
		if (e != NULL)
			return (e);
		mode = gs.st_mode;
		gfs_stat_free(&gs);
		if (!GFARM_S_IS_FRAGMENTED_FILE(mode)) {
			gfarmfs_gfrep_N_init();
			rep_mode = REP_GFREP_N;
		} else
#endif /* FIX_GFREP_H */
		if (enable_replication == REP_GFREP_H) { /* normal */
			if (nhosts > 0) {
				hosts = calloc(nhosts, sizeof(char *));
				if (hosts == NULL)
					return (GFARM_ERR_NO_MEMORY);
			} else {
				/* do nothing */
				return (NULL);
			}
			e = gfarmfs_gfrep_H_init(url, &nhosts, hosts);
			if (e != NULL) {
				free(hosts);
				return (e); /* error */
			}
			if (nhosts == 0) {
				/* do nothing */
				free(hosts);
				return (NULL);
			} else if (nhosts > 0) {
				rep_mode = REP_GFREP_H;
			} else {  /* nhosts == -1 -> using gfrep -N */
				free(hosts);
				hosts = NULL;
				gfarmfs_gfrep_N_init();
				rep_mode = REP_GFREP_N;
			}
		} else {
			/* unexpected: do nothing */
			return (NULL);
		}
#else  /* not use gfrep -H */
		/* unexpected: do nothing */
		return (NULL);
#endif  /* USE_GFREP_H */
	}
	e = gfarmfs_async_fork_count_increment(); /* limiter */
	if (e != NULL)
		return (e);
	*fh->shm_child_status = CHILD_RUNNING; /* gfarmfs_async_start(fh) */
	fh->child_pid = fork();
	if (fh->child_pid == -1) {
		/* error */
		int save_errno = errno;
		gfarmfs_errlog("REPLICATE: fork: %s", strerror(save_errno));
		(void)gfarmfs_async_fork_count_decrement();
		*fh->shm_child_status = CHILD_DONE;
		e = gfarm_errno_to_error(save_errno);
	} else if (fh->child_pid == 0) {
		/* child */
		if (rep_mode == REP_GFREP_N)
			gfarmfs_gfrep_N_exec(url);
#ifdef USE_GFREP_H
		else if (rep_mode == REP_GFREP_H)
			gfarmfs_gfrep_H_exec(url, nhosts, hosts);
#endif
#ifdef USE_GFARM_SCRAMBLE
		else if (rep_mode == REP_SCRAMBLE)
			gfarmfs_scramble_replicate(url);
#endif
		/* done */
		(void)gfarmfs_async_fork_count_decrement();
		*fh->shm_child_status = CHILD_DONE;
		_exit(0);
	}
	/* parent: continue */
#ifdef USE_GFREP_H
	if (hosts != NULL) {
		int i;
		for (i = 0; i < nhosts; i++)
			if (hosts[i])
				free(hosts[i]);
		free(hosts);
	}
#endif
	return (e);
}
#endif

static char *
gfarmfs_fh_alloc(FH *fhp)
{
	FH fh;

	fh = malloc(sizeof(*fh));
	if (fh == NULL)
		return (GFARM_ERR_NO_MEMORY);
	fh->gf = NULL;
	fh->flags = 0;
	fh->nopen = 0;
	fh->nwrite = 0;
#if FH_LIST_USE_INO == 1
	/* fh->ino = -1 */
#else
	fh->path = NULL;
#endif
#if REVISE_CHMOD
	fh->mode_changed = 0;
#endif
#if REVISE_UTIME
	fh->utime_changed = 0;
#endif
#if ENABLE_ASYNC_REPLICATION
	fh->shm_child_status = NULL;
	fh->child_pid = -1;
#endif
	*fhp = fh;
	return (NULL);
}

static char *
gfs_pio_open_common(char *url, int flags, GFS_File *gfp, mode_t *create_modep)
{
	char *e;

	if (enable_gfarm_iobuf == 0) {
		flags |= GFARM_FILE_UNBUFFERED;
	}
#ifdef ENABLE_FASTCREATE
	/* created a file on MKNOD */
	if (enable_fastcreate > 0) {
		e = gfarmfs_fastcreate_open(url, flags, gfp);
	} else {
		e = gfs_pio_open(url, flags, gfp);
		if (e != NULL)
			return (e);
		e = gfarmfs_set_view_using_url(*gfp, url);
		if (e != NULL)
			gfs_pio_close(*gfp);
	}
#else
	if (create_modep) {
		e = gfs_pio_create(
			url,
			flags|GFARM_FILE_TRUNC|GFARM_FILE_EXCLUSIVE,
			*create_modep, gfp);
		if (e != NULL)
			return (e);
		e = gfarmfs_set_view_using_mode(*gfp, *create_modep);
	} else {
		e = gfs_pio_open(url, flags, gfp);
		if (e != NULL)
			return (e);
		e = gfarmfs_set_view_using_url(*gfp, url);
	}
	if (e != NULL)
		gfs_pio_close(*gfp);
#endif
	return (e);
}

static int
gfarmfs_open_common_share_gf(char *opname,
			     const char *path, struct fuse_file_info *fi,
			     mode_t *create_modep)
{
	char *e;
	char *url;
	struct gfs_stat gs;
	FH fh;
	long ino = 0;
	mode_t save_mode = 0;
	GFS_File gf;
#ifdef ENABLE_FASTCREATE
	struct stat st;
#endif

	if ((e = gfarmfs_init()) != NULL) goto end;
	e = add_gfarm_prefix(path, &url);
	if (e != NULL) goto end;

	if (create_modep) { /* normal CREATE */
		fh = NULL;
#ifdef ENABLE_FASTCREATE
	} else if (enable_fastcreate > 0 &&
		   gfarmfs_fastcreate_getattr(path, &st)) {
		/* fastcreate */
		create_modep = &st.st_mode;
		fh = NULL;
#endif
	} else {  /* normal OPEN */
		e = gfs_stat(url, &gs);
		if (e != NULL) goto free_url;
		ino = gs.st_ino;
		save_mode = gs.st_mode;
		gfs_stat_free(&gs);
		fh = FH_GET2(url, ino);
#if ENABLE_ASYNC_REPLICATION
		gfarmfs_async_wait(fh);
		if (fh != NULL && !gfarmfs_fh_is_opened(fh)) {
			FH_REMOVE2(url, ino);
			FH_FREE(fh);
			fh = NULL;
		}
#endif
	}
	if (fh == NULL) {  /* new gfarmfs_fh */
		e = gfarmfs_fh_alloc(&fh);
		if (e != NULL) goto free_url;
		if ((fi->flags & O_ACCMODE) == O_RDONLY) {
			e = gfs_pio_open_common(url, GFARM_FILE_RDONLY, &gf,
						create_modep);
			if (e == NULL) {
				fh->gf = gf;
				fh->flags = GFARM_FILE_RDONLY;
				fh->nopen++;
			}
		} else {  /* WRONLY or RDWR */
			e = gfs_pio_open_common(url, GFARM_FILE_RDWR, &gf,
						create_modep);
			if (e == NULL) {
				fh->flags = GFARM_FILE_RDWR;
			} else if ((fi->flags & O_ACCMODE) == O_WRONLY) {
				/* minor case (mode is 0200) */
				e = gfs_pio_open_common(
					url, GFARM_FILE_WRONLY, &gf,
					create_modep);
				if (e == NULL)
					fh->flags = GFARM_FILE_WRONLY;
			}
			if (e == NULL) {
				fh->gf = gf;
				fh->nopen++;
				fh->nwrite++;
			}
		}
#if FH_LIST_USE_INO == 1  /* key is ino */
		if (create_modep && e == NULL) { /* get st_ino */
			e = gfs_fstat(gf, &gs);
			if (e == NULL) {
				ino = gs.st_ino;
				gfs_stat_free(&gs);
			}
		}
#endif
		if (e == NULL) {
			FH fh2 = FH_GET2(url, ino);
#if ENABLE_ASYNC_REPLICATION
			if (fh2 != NULL && !gfarmfs_fh_is_opened(fh2)) {
				FH_REMOVE2(url, ino); /* for FH_ADD */
				FH_FREE(fh2);
				fh2 = NULL;
			}
#endif
			if (fh2 != NULL)
				e = GFARM_ERR_ALREADY_EXISTS;
			else
				e = FH_ADD2(url, ino, fh);
		}
		if (e != NULL) {
			if (fh->gf)
				gfs_pio_close(fh->gf);
			FH_FREE(fh);
		}
	} else if ((fh->flags & GFARM_FILE_ACCMODE) == GFARM_FILE_RDWR) {
		fh->nopen++;
		if ((fi->flags & O_ACCMODE) != O_RDONLY)
			fh->nwrite++;
	} else if ((fh->flags & GFARM_FILE_ACCMODE) == GFARM_FILE_RDONLY &&
		   (fi->flags & O_ACCMODE) == O_RDONLY) {
		fh->nopen++;
	} else if ((fh->flags & GFARM_FILE_ACCMODE) == GFARM_FILE_WRONLY &&
		   (fi->flags & O_ACCMODE) == O_WRONLY) {
		fh->nopen++;
		fh->nwrite++;
	} else {  /* RDONLY or WRONLY -> RDWR */
		e = gfs_pio_open_common(url, GFARM_FILE_RDWR, &gf, NULL);
		if (e != NULL) {
			/* retry */
			if ((fi->flags & O_ACCMODE) == O_RDONLY)
				e = gfs_access(url, R_OK);
			else if ((fi->flags & O_ACCMODE) == O_WRONLY)
				e = gfs_access(url, W_OK);
			if (e == NULL)
				e = gfs_chmod(url, save_mode|0600);
			if (e == NULL) {
				char *e2;
				e = gfs_pio_open_common(
					url, GFARM_FILE_RDWR, &gf, NULL);
#if REVISE_CHMOD
				/* must chmod on release */
				fh->mode_changed = 1;
				fh->save_mode = save_mode;
				e2 = gfs_chmod(url, save_mode);
#else
				/* gfs_fchmod cannot work in global view
				 * (the case of nsection >= 2)
				 * on gfarm v1.3.1 or earlier
				 */
				if (e == NULL)
					e2 = gfs_fchmod(gf, save_mode);
				else
					e2 = gfs_chmod(url, save_mode);
#endif
				if (e2 != NULL) {
					/* What happen ? */
					gfarmfs_errlog("OPEN: WARN: "
						       "chmod failed: "
						       "%o: %s: %s",
						       save_mode, path, e2);
				}
			}
		}
		if (e == NULL) {
			(void)gfs_pio_close(fh->gf);
			fh->gf = gf;
			fh->flags = GFARM_FILE_RDWR;
			fh->nopen++;
			if ((fi->flags & O_ACCMODE) != O_RDONLY)
				fh->nwrite++;
		}
	}
	if (e == NULL)
		fi->fh = (unsigned long) fh;
free_url:
	free(url);
end:
	return gfarmfs_final(opname, e, 0, path);
}

static int
gfarmfs_open_share_gf(const char *path, struct fuse_file_info *fi)
{
	return gfarmfs_open_common_share_gf("OPEN", path, fi, NULL);
}

#if FUSE_USE_VERSION >= 25
static int
gfarmfs_create_share_gf(const char *path, mode_t mode,
			struct fuse_file_info *fi)
{
	return gfarmfs_open_common_share_gf("CREATE", path, fi, &mode);
}
#endif

#if REVISE_CHMOD
#define IS_EXECUTABLE(mode)  ((mode) & 0111 ? 1 : 0)
#endif

static char *
gfarmfs_chmod_share_gf_internal(char *url, mode_t mode)
{
	char *e;
	struct gfs_stat gs;
	FH fh;

	e = gfs_stat(url, &gs);
	if (e != NULL)
		goto end;
	if (!GFARM_S_ISREG(gs.st_mode)) {
		e = gfs_chmod(url, mode);
		goto free_gfs_stat;
	}
	/* regular file */
	fh = FH_GET2(url, gs.st_ino);
#if ENABLE_ASYNC_REPLICATION
	gfarmfs_async_wait(fh);
	if (!gfarmfs_fh_is_opened(fh))
		fh = NULL; /* closed */
#endif
#if REVISE_CHMOD
	if (fh != NULL) { /* somebody opens this file */
		/* must chmod on release */
		fh->mode_changed = 1;
		fh->save_mode = mode;
		if (IS_EXECUTABLE(gs.st_mode) == IS_EXECUTABLE(mode)) {
			e = gfs_chmod(url, mode);
			if (e != NULL)
				fh->mode_changed = 0;
		}
		/* (else): somebody opens this file and changes executable bit.
		 * -> gfs_chmod on RELEASE only
		 * In Gfarm v1, gfs_pio_close fails at replacing section info
		 * in this case.
		 */
		/* always succeeed ... (e == NULL) */
	} else {
		/* nobody opens this file */
		e = gfs_chmod(url, mode);
	}
#else
	if (fh != NULL) /* somebody opens this open */
		/* gfs_fchmod cannot work in global view
		 * (the case of nsection >= 2) on gfarm v1.3.1 or earlier
		 */
		e = gfs_fchmod(fh->gf, mode);
	else
		e = gfs_chmod(url, mode);
#endif
free_gfs_stat:
	gfs_stat_free(&gs);
end:
	return (e);
}

static int
gfarmfs_chmod_share_gf(const char *path, mode_t mode)
{
	char *e;
	char *url;

	gfarmfs_fastcreate_check();
	e = gfarmfs_init();
	if (e != NULL) goto end;
	e = add_gfarm_prefix(path, &url);
	if (e != NULL) goto end;
	e = gfarmfs_chmod_share_gf_internal(url, mode);
	free(url);
end:
	return gfarmfs_final("CHMOD", e, 0, path);
}

static char *
gfarmfs_utime_share_gf_internal(char *url, struct utimbuf *buf)
{
	char *e;
	struct gfarm_timespec gt[2];

	if (buf == NULL)
		e = gfs_utimes(url, NULL);
	else {
		gt[0].tv_sec = buf->actime;
		gt[0].tv_nsec= 0;
		gt[1].tv_sec = buf->modtime;
		gt[1].tv_nsec= 0;
		e = gfs_utimes(url, gt);
	}
#if REVISE_UTIME
	if (e == NULL) {
		FH fh;
		fh = FH_GET1(url);
#if ENABLE_ASYNC_REPLICATION
		if (!gfarmfs_fh_is_opened(fh))
			fh = NULL;
#endif
		if (fh != NULL) {
			/* must utime on release */
			fh->utime_changed = 1;
			if (buf == NULL) {
				time_t t;
				time(&t);
				fh->save_utime[0].tv_sec = t;
				fh->save_utime[0].tv_nsec = 0;
				fh->save_utime[1].tv_sec = t;
				fh->save_utime[1].tv_nsec = 0;
			} else {
				fh->save_utime[0] = gt[0];
				fh->save_utime[1] = gt[1];
			}
		}
	}
#endif
	return (e);
}

static int
gfarmfs_utime_share_gf(const char *path, struct utimbuf *buf)
{
	char *e;
	char *url;

	gfarmfs_fastcreate_check();
	e = gfarmfs_init();
	if (e != NULL) goto end;
	e = add_gfarm_prefix(path, &url);
	if (e != NULL) goto end;
	e = gfarmfs_utime_share_gf_internal(url, buf);
	free(url);
end:
	return gfarmfs_final("UTIME", e, 0, path);
}

static char *
gfarmfs_rename_share_gf_check_open(char *from_url, char *to_url,
				   long from_ino, mode_t from_mode)
{
	char *e;
	FH fh;

#if GFARM_USE_VERSION == 1   /* for Gfarm version 1 */
	fh = FH_GET2(from_url, from_ino);
#if ENABLE_ASYNC_REPLICATION
	gfarmfs_async_wait(fh);
	if (fh != NULL && !gfarmfs_fh_is_opened(fh)) {
		FH_REMOVE2(from_url, from_ino); /* for FH_ADD */
		FH_FREE(fh);
		fh = NULL; /* closed */
	}
#endif
	if (fh != NULL) { /* opened */
		FH_REMOVE2(from_url, from_ino);
		gfs_pio_close(fh->gf);
		/* so that gfs_pio_close can set metadata correctly */
	}
	e = gfs_rename(from_url, to_url);
	if (fh != NULL) { /* somebody opens this file */
		char *e2;
		char *url;
		GFS_File gf;
		int retry;
		if (e == NULL)
			url = to_url; /* renamed */
		else
			url = from_url; /* recover */
		retry = 0;
	retry:
		if (retry) {
			char *e3;
			e3 = gfs_chmod(url, from_mode|0600);
			if (e3 == NULL)
				fh->flags = GFARM_FILE_RDWR;
			else
				return (e);
		}
		e2 = gfs_pio_open_common(url, fh->flags, &gf, NULL);
		if (retry) {
			char *e3;
#if REVISE_CHMOD
			/* must chmod on release */
			fh->mode_changed = 1;
			fh->save_mode = from_mode;
			e3 = gfs_chmod(url, from_mode);
#else
			if (e2 == NULL)
				/* gfs_fchmod cannot work in global view
				 * (the case of nsection >= 2)
				 * on gfarm v1.3.1 or earlier
				 */
				e3 = gfs_fchmod(gf, from_mode);
			else
				e3 = gfs_chmod(url, from_mode);
#endif
			if (e3 != NULL) {
				/* What happen ? */
				gfarmfs_errlog("RENAME: WARN: "
					       "chmod failed: %o: %s: %s",
					       from_mode, gfarm_url2path(url),
					       e3);
			}
		}
		if (e2 == NULL) { /* open succeeeded */
			struct gfs_stat gs;
			fh->gf = gf;
			e2 = gfs_fstat(gf, &gs);
			if (e2 == NULL) {
				e2 = FH_ADD2(url, gs.st_ino, fh);
				gfs_stat_free(&gs);
			} else {
				/* What happen ? */
				gfarmfs_errlog("RENAME: FATAL: "
					       "some problem may happen later:"
					       " %s", gfarm_url2path(url));
			}
		} else { /* somebody changes st_mode */
			if (retry == 0) {
				retry = 1;
				goto retry;
			}
			/* fatal situation ! */
			fh->gf = NULL;
			gfarmfs_errlog("RENAME: FATAL: "
				       "can't read/write more than this: "
				       "%s: %s", gfarm_url2path(url), e2);
			/* ignore e2 */
		}
	} /* (fh != NULL) */
#else   /* for Gfarm version 2 or lator */
	fh = FH_GET2(from_url, from_ino);
#if ENABLE_ASYNC_REPLICATION
	gfarmfs_async_wait(fh);
	if (fh != NULL && !gfarmfs_fh_is_opened(fh)) {
		FH_REMOVE2(from_url, from_ino); /* for FH_ADD */
		FH_FREE(fh);
		fh = NULL;
	}
#endif
	if (fh != NULL)
		FH_REMOVE2(from_url, from_ino);
	e = gfs_rename(from_url, to_url);
	if (fh != NULL) {
		if (e == NULL)
			e = FH_ADD1(to_url, fh);
		else
			e = FH_ADD2(from_url, from_ino, fh);
	}
#endif
	return (e);
}

static int
gfarmfs_rename_share_gf(const char *from, const char *to)
{
	char *e;
	char *from_url;
	char *to_url;
	struct gfs_stat gs;
	long save_ino;
	mode_t save_mode;

	gfarmfs_fastcreate_check();
	e = gfarmfs_init();
	if (e != NULL) goto end;
	e = add_gfarm_prefix(from, &from_url);
	if (e != NULL) goto end;
	e = add_gfarm_prefix(to, &to_url);
	if (e != NULL) goto free_from_url;
	e = gfs_stat(from_url, &gs);
#ifdef SYMLINK_MODE
	if (enable_symlink == 1 && e == GFARM_ERR_NO_SUCH_OBJECT) {
		free(from_url);
		free(to_url);
		e = add_gfarm_prefix_symlink_suffix(from, &from_url);
		if (e != NULL) goto end;
		e = add_gfarm_prefix_symlink_suffix(to, &to_url);
		if (e != NULL) goto free_from_url;
		if (gfarmfs_debug >= 2) {
			printf("RENAME: for symlink: %s\n", from);
		}
		e = gfs_stat(from_url, &gs);
	}
#endif
	if (e != NULL)
		goto free_to_url;
	if (GFARM_S_ISDIR(gs.st_mode)) {
		gfs_stat_free(&gs);
		/* wait all background processes of replication */
		gfarmfs_async_fork_count_increment_n(DEFAULT_FORK_MAX);
		wait(NULL);
		e = gfs_rename(from_url, to_url);
		gfarmfs_async_fork_count_decrement_n(DEFAULT_FORK_MAX);
	} else if (GFARM_S_ISREG(gs.st_mode)) {
		save_ino = gs.st_ino;
		save_mode = gs.st_mode;
		gfs_stat_free(&gs);
		e = gfarmfs_rename_share_gf_check_open(from_url, to_url,
						       save_ino, save_mode);
	} else {
		e = gfs_rename(from_url, to_url);
	}
free_to_url:
	free(to_url);
free_from_url:
	free(from_url);
end:
	return gfarmfs_final("RENAME", e, 0, from);
}

static int
gfarmfs_getattr_share_gf(const char *path, struct stat *stbuf)
{
	char *e;
	FH fh;
	struct gfs_stat gs1, gs2;
	int symlinkmode = 0;
	char *url;

	if ((e = gfarmfs_init()) != NULL) goto end;
#ifdef ENABLE_FASTCREATE
	if (enable_fastcreate > 0 && gfarmfs_fastcreate_getattr(path, stbuf))
		goto end;
#endif
	e = add_gfarm_prefix(path, &url);
	if (e != NULL) goto end;
	e = gfs_stat(url, &gs1);
#ifdef SYMLINK_MODE
	if (enable_symlink == 1 && e == GFARM_ERR_NO_SUCH_OBJECT) {
		free(url);
		e = add_gfarm_prefix_symlink_suffix(path, &url);
		if (e != NULL) goto end;
		e = gfs_stat(url, &gs1);
		symlinkmode = 1;
	}
#endif
	if (e != NULL)
		goto free_url;
	e = convert_gfs_stat_to_stat(url, &gs1, stbuf, symlinkmode);
	if (e != NULL)
		goto free_gfs_stat;
	if (!S_ISREG(stbuf->st_mode)) /* && e == NULL: skip */
		goto free_gfs_stat;
	fh = FH_GET2(url, gs1.st_ino);
#if ENABLE_ASYNC_REPLICATION
	if (!gfarmfs_fh_is_opened(fh))
		fh = NULL;
#endif
	/* gfs_stat cannot get the exact st_size while the file is opened.
	 * But gfs_fstat can do it.
	 */
	if (fh != NULL) {  /* somebody opens this file. */
#if REVISE_CHMOD
		if (fh->mode_changed)
			stbuf->st_mode = fh->save_mode;
#endif
		e = gfs_fstat(fh->gf, &gs2);
		if (e == NULL) {
			stbuf->st_size = gs2.st_size;
			gfs_stat_free(&gs2);
		}
		/* else: e = NULL (need?) */
	} else if (enable_exact_filesize == 1 && S_ISREG(stbuf->st_mode)) {
		char *e2;
		file_offset_t size;
		e2 = gfarmfs_exact_filesize(url, &size, stbuf->st_mode);
		if (e2 == NULL)
			stbuf->st_size = size;
	}
free_gfs_stat:
	gfs_stat_free(&gs1);
free_url:
	free(url);
end:
	return gfarmfs_final("GETATTR", e, 0, path);
}

static inline GFS_File
gfarmfs_cast_fh_share_gf(struct fuse_file_info *fi)
{
	FH fh;

	fh = (FH)(unsigned long) fi->fh;
	if (fh != NULL)
		return (fh->gf);
	else
		return (NULL);
}

static int
gfarmfs_release_share_gf(const char *path, struct fuse_file_info *fi)
{
	char *e;
	FH fh;

	gfarmfs_fastcreate_check();
	if ((e = gfarmfs_init()) != NULL) goto end;

	fh = (FH)(unsigned long) fi->fh;
	fh->nopen--;
	if (fh->nopen <= 0) {
		char *url;
		char *e2;
		if (fh->gf)
			e = gfs_pio_close(fh->gf);
		else
			e = NULL; /* GFARM_ERR_INPUT_OUTPUT ? */
		e2 = add_gfarm_prefix(path, &url);
		if (e2 != NULL) {
			if (e == NULL)
				e = e2;
			goto end;
		} else {
#if REVISE_CHMOD
			if (fh->mode_changed)
				e = gfs_chmod(url, fh->save_mode);
#endif
#if REVISE_UTIME
			if (fh->utime_changed)
				e = gfs_utimes(url, fh->save_utime);
#endif
		}
#if ENABLE_ASYNC_REPLICATION
		if (enable_replication != REP_DISABLE /* enable */ &&
		    (fh->flags & GFARM_FILE_ACCMODE) != GFARM_FILE_RDONLY &&
		    e == NULL) {
			e = gfarmfs_async_replicate(url, fh);
			if (e != NULL) {
				gfarmfs_errlog("REPLICATE: %s", e);
				e = NULL;
			}
		} else { /* disable or read-open or error */
			FH_REMOVE2(url, fh->ino);
			FH_FREE(fh);
		}
#else
		FH_REMOVE2(url, fh->ino);
		FH_FREE(fh);
#endif
		free(url);
	} else {   /* nopen > 0 */
		if ((fi->flags & O_ACCMODE) != O_RDONLY)
			fh->nwrite--;
		if (fh->nwrite <= 0 &&
		    (fh->flags & GFARM_FILE_ACCMODE) != GFARM_FILE_RDONLY) {
			/* try to change flags into O_RDONLY */
			char *url;
			GFS_File gf;
			e = add_gfarm_prefix(path, &url);
			if (e != NULL) {
				e = NULL; /* ignore */
				goto end;
			}
			e = gfs_pio_open(url, GFARM_FILE_RDONLY, &gf);
			free(url);
			if (e != NULL)
				e = NULL; /* use current fh->gf */
			else {
				if (fh->gf)
					gfs_pio_close(fh->gf);
				fh->gf = gf;
				fh->flags = GFARM_FILE_RDONLY;
			}
		}
	}
end:
	return gfarmfs_final(OP_RELEASE, e, 0, path);
}

static int
gfarmfs_truncate_share_gf(const char *path, off_t size)
{
	char *e;
	char *url;
	FH fh;

	if ((e = gfarmfs_init()) != NULL) goto end;
	e = add_gfarm_prefix(path, &url);
	if (e != NULL) goto end;
	fh = FH_GET1(url);
#if ENABLE_ASYNC_REPLICATION
	gfarmfs_async_wait(fh);
	if (!gfarmfs_fh_is_opened(fh))
		fh = NULL;
#endif
	if (fh != NULL)
		e = gfs_pio_truncate(fh->gf, size);
	else
		e = gfarmfs_truncate_common(url, size);
	free(url);
end:
	return gfarmfs_final("TRUNCATE", e, 0, path);
}

#if FUSE_USE_VERSION >= 25
static int
gfarmfs_fgetattr_share_gf(const char *path, struct stat *stbuf,
			  struct fuse_file_info *fi)
{
	char *e;
	struct gfs_stat gs;
	FH fh;

	if ((e = gfarmfs_init()) != NULL) goto end;
	fh = (FH)(unsigned long) fi->fh;
	if (fh->gf == NULL)
		e = GFARM_ERR_INPUT_OUTPUT;
	else
		e = gfs_fstat(fh->gf, &gs);
	if (e == NULL) {
		e = convert_gfs_stat_to_stat(NULL, &gs, stbuf, 0);
		gfs_stat_free(&gs);
#if REVISE_CHMOD
		if (fh->mode_changed)
			stbuf->st_mode = fh->save_mode;
#endif
	}
end:
	return gfarmfs_final("FGETATTR", e, 0, path);
}

static int
gfarmfs_ftruncate_share_gf(const char *path, off_t size,
			   struct fuse_file_info *fi)
{
	char *e;
	FH fh;

	if ((e = gfarmfs_init()) != NULL) goto end;
	fh = (FH)(unsigned long) fi->fh;
	if (fh->gf == NULL)
		e = GFARM_ERR_INPUT_OUTPUT;
	else
		e = gfs_pio_truncate(fh->gf, size);
#if REVISE_UTIME
	if (e == NULL)
		fh->utime_changed = 0; /* reset */
#endif
end:
	return gfarmfs_final("FTRUNCATE", e, 0, path);
}
#endif /* FUSE_USE_VERSION >= 25 */

static int
gfarmfs_read_share_gf(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	char *e;
	file_offset_t off;
	int n = 0;
	FH fh;

	if ((e = gfarmfs_init()) != NULL) goto end;
	fh = (FH)(unsigned long) fi->fh;
	if (fh->gf == NULL)
		e = GFARM_ERR_INPUT_OUTPUT;
	else
		e = gfs_pio_seek(fh->gf, offset, 0, &off);
	if (e != NULL) goto end;
	e = gfs_pio_read(fh->gf, buf, size, &n);
#if REVISE_UTIME
	if (n > 0)
		fh->utime_changed = 0; /* reset */
#endif
end:
	return gfarmfs_final("READ", e, n, path);
}

static int
gfarmfs_write_share_gf(const char *path, const char *buf, size_t size,
		       off_t offset, struct fuse_file_info *fi)
{
	char *e;
	file_offset_t off;
	int n = 0;
	FH fh;

	if ((e = gfarmfs_init()) != NULL) goto end;
	fh = (FH)(unsigned long) fi->fh;
	if (fh->gf == NULL)
		e = GFARM_ERR_INPUT_OUTPUT;
	else
		e = gfs_pio_seek(fh->gf, offset, 0, &off);
	if (e != NULL) goto end;
	e = gfs_pio_write(fh->gf, buf, size, &n);
#if REVISE_UTIME
	if (n > 0)
		fh->utime_changed = 0; /* reset */
#endif
end:
	return gfarmfs_final("WRITE", e, n, path);
}

static int
gfarmfs_fsync_share_gf(const char *path, int isdatasync,
		       struct fuse_file_info *fi)
{
	char *e;
	GFS_File gf;

	if ((e = gfarmfs_init()) != NULL) goto end;
	gf = gfarmfs_cast_fh_share_gf(fi);
	if (gf == NULL)
		e = GFARM_ERR_INPUT_OUTPUT;
	else {
		if (isdatasync)
			e = gfs_pio_datasync(gf);
		else
			e = gfs_pio_sync(gf);
	}
end:
	return gfarmfs_final("FSYNC", e, 0, path);
}

static int
gfarmfs_unlink_share_gf(const char *path)
{
#if ENABLE_ASYNC_REPLICATION
	if (enable_replication != REP_DISABLE) {
		char *e;
		char *url;
		FH fh;
		e = add_gfarm_prefix(path, &url);
		if (e == NULL) {
			fh = FH_GET1(url);
			gfarmfs_async_stop(fh); /* for cancel... */
			gfarmfs_async_wait(fh);
			free(url);
		}
	}
#endif
	return gfarmfs_unlink(path);
}

static struct fuse_operations gfarmfs_oper_share_gf = {
	.getattr   = gfarmfs_getattr_share_gf,
	.readlink  = gfarmfs_readlink,
	.getdir    = gfarmfs_getdir,
	.mknod     = gfarmfs_mknod,
	.mkdir     = gfarmfs_mkdir,
	.symlink   = gfarmfs_symlink,
	.unlink    = gfarmfs_unlink_share_gf,
	.rmdir     = gfarmfs_rmdir,
	.rename    = gfarmfs_rename_share_gf,
	.link      = gfarmfs_link,
	.chmod     = gfarmfs_chmod_share_gf,
	.chown     = gfarmfs_chown,
	.truncate  = gfarmfs_truncate_share_gf,
	.utime     = gfarmfs_utime_share_gf,
	.open      = gfarmfs_open_share_gf,
	.read      = gfarmfs_read_share_gf,
	.write     = gfarmfs_write_share_gf,
	.release   = gfarmfs_release_share_gf,
	.fsync     = gfarmfs_fsync_share_gf,
#ifdef USE_GFS_STATFSNODE
	.statfs    = gfarmfs_statfs,
#endif
#if FUSE_USE_VERSION >= 25
	.create    = gfarmfs_create_share_gf,
	.fgetattr  = gfarmfs_fgetattr_share_gf,
	.ftruncate = gfarmfs_ftruncate_share_gf,
	.access    = gfarmfs_access,
#endif
	.flush     = gfarmfs_flush,
#if ENABLE_XATTR
	.setxattr    = gfarmfs_setxattr,
	.getxattr    = gfarmfs_getxattr,
	.listxattr   = gfarmfs_listxattr,
	.removexattr = gfarmfs_removexattr,
#endif
};

/* ################################################################### */

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif

static int
iscommand(char *path)
{
	struct stat sb;

	if (stat(path, &sb) == 0) {
		if (S_ISREG(sb.st_mode) &&
		    /* save time by not calling access() */
		    (sb.st_mode & (S_IXOTH|S_IXGRP|S_IXUSR)) &&
		    access(path, X_OK) == 0)
			return (1);
	}
	return (0);
}

static char *
commandpath(char *command)
{
	static int bufsize = 0;
	static char *buffer = NULL;
	char *path;
	int span, dirlen;

	if (strchr(command, '/') != NULL) {     /* absolute or relative path */
		return iscommand(command) ? command : NULL;
	}

	if ((path = getenv("PATH")) == NULL) {
#ifdef _PATH_DEFPATH
		path = _PATH_DEFPATH;
#else
		path = "/bin:/usr/bin"; /* XXX */
#endif
	}
	if (buffer == NULL) {
#ifdef PATH_MAX
		bufsize = PATH_MAX + 1;
#else
		bufsize = 1025;
#endif
		if ((buffer = malloc(bufsize)) == NULL) {
			fprintf(stderr, "commandpath(%s): not enough memory\n",
				command);
			exit(1);
		}
	}
	for (;;) {
		span = strcspn(path, ":");
		dirlen = span == 0 ? 1 : span;
		if (bufsize <= dirlen + 1 + strlen(command)) {
			bufsize = dirlen + 1 + strlen(command) + 1;
			if ((buffer = realloc(buffer, bufsize)) == NULL) {
				fprintf(stderr,
					"commandpath(%s): not enough memory\n",
					command);
				exit(1);
			}
		}
		if (span == 0)
			buffer[0] = '.';
		else
			memcpy(buffer, path, dirlen);
		buffer[dirlen] = '/';
		strcpy(&buffer[dirlen + 1], command);
		if (iscommand(buffer))
			return buffer;
		if (path[span] == '\0')
			break;
		path += span + 1; /* skip ':' */
	}
	return (NULL); /* not found */
}

/* ################################################################### */

static char *program_name = "gfarmfs";
static struct fuse_operations *gfarmfs_oper_p = &gfarmfs_oper_share_gf;

static void
gfarmfs_version(int print_fuse_version)
{
	const char *fusever[] = { program_name, "-V", NULL };

	printf("GfarmFS-FUSE version %s (%s)\n",
	       GFARMFS_VER, GFARMFS_VER_DATE);
	printf("Build: %s %s\n", __DATE__, __TIME__);
#if FUSE_USE_VERSION >= 25
	printf("using FUSE version 2.5 interface\n");
#else
	printf("using FUSE version 2.2 interface\n");
#endif
	if (print_fuse_version)
		fuse_main(2, (char **) fusever, gfarmfs_oper_p);
}

static void
gfarmfs_usage()
{
	const char *fusehelp[] = { program_name, "-ho", NULL };

	fprintf(stderr,
"Usage: %s [GfarmFS options] <mountpoint> [FUSE options]\n"
"\n"
"GfarmFS options:\n"
"    -m <dir on Gfarm>      set mount point on Gfarm\n"
"                           (ex. -m /username cut gfarm:/username)\n"
#ifdef SYMLINK_MODE
"    -s, --symlink          enable symlink(2) to work (emulation)\n"
#endif
"    -l, --linkiscopy       enable link(2) to behave copying a file (emulation)\n"
"    -u, --unlinkall        enable unlink(2) to remove all architecture files\n"
"    -b, --buffered         enable buffered I/O (unset GFARM_FILE_UNBUFFERED)\n"
"    -F, --exactfilesize    during open(2), exact st_size for other clients\n"
"    -n, --dirnlink         count nlink of a directory precisely\n"
#ifdef USE_GFS_STATFSNODE
"    -S, --disable-statfs   disable statfs(2)\n"
#endif
#if ENABLE_ASYNC_REPLICATION
"    -N <num-of-replicas>   run gfrep command after write-close\n"
"    -D <domainname>        domainname of destination for -N option\n"
#endif
"    -a <architecture>      for a client not registered by gfhost\n"
"    --trace <filename>     record FUSE operations called by processes\n"
"    --errlog <filename>    record errors of gfarm's own and replication\n"
"    -v, --version          show version and exit\n"
"\n", program_name);

	fuse_main(2, (char **) fusehelp, gfarmfs_oper_p);
}

static void
check_fuse_options(int *argcp, char ***argvp, int *newargcp, char ***newargvp)
{
	int argc = *argcp;
	char **argv = *argvp;
	char **newargv;
	int i;
	int ok_s = 0; /* check -s */
	char *opt_s_str = "-s";

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-s") == 0) {
			ok_s = 1;
		} else if (strcmp(argv[i], "-f") == 0) {
			gfarmfs_debug = 1;
		} else if (strcmp(argv[i], "-d") == 0) {
			gfarmfs_debug = 2;
		} else if (strcmp(argv[i], "-h") == 0) {
			gfarmfs_usage();
			exit(0);
		}
	}
	if (ok_s == 0) { /* add -s option */
		newargv = malloc((argc + 1) * sizeof(char *));
		if (newargv == NULL) {
			errno = ENOMEM;
			perror("");
			exit(1);
		}
		for (i = 0; i < argc; i++) {
			newargv[i] = argv[i];
		}
		argc++;
		newargv[argc - 1] = opt_s_str;
		*newargcp = argc;
		*newargvp = newargv;
	} else {
		*newargcp = argc;
		*newargvp = argv;
	}
}

static int
next_arg_set(char **valp, int *argcp, char ***argvp, int errorexit)
{
	int argc = *argcp;
	char **argv = *argvp;
	
	--argc;
	++argv;
	if (argc <= 0 ||
	    (argc > 0 && argv[0][0] == '-' && argv[0][1] != '\0')) {
		if (errorexit) {
			gfarmfs_usage();
			exit(1);
		} else {
			return (0);
		}
	}
	*valp = *argv;
	*argcp = argc;
	*argvp = argv;
	return (1);
}

static void
parse_long_option(int *argcp, char ***argvp)
{
	char **argv = *argvp;

	if (strcmp(&argv[0][1], "-symlink") == 0)
		enable_symlink = 1;
	else if (strcmp(&argv[0][1], "-linkiscopy") == 0)
		enable_linkiscopy = 1;
	else if (strcmp(&argv[0][1], "-architecture") == 0)
		next_arg_set(&arch_name, argcp, argvp, 1);
	else if (strcmp(&argv[0][1], "-unlinkall") == 0)
		enable_unlinkall = 1;
	else if (strcmp(&argv[0][1], "-unbuffered") == 0)
		enable_gfarm_iobuf = 0;
	else if (strcmp(&argv[0][1], "-buffered") == 0)
		enable_gfarm_iobuf = 1;
	else if (strcmp(&argv[0][1], "-dirnlink") == 0)
		enable_count_dir_nlink = 1;
	else if (strcmp(&argv[0][1], "--exactfilesize") == 0)
		enable_exact_filesize = 1;
#ifdef USE_GFS_STATFSNODE
	else if (strcmp(&argv[0][1], "-disable-statfs") == 0)
		enable_statfs = 0;
#endif
	else if (strcmp(&argv[0][1], "-oldio") == 0)
		use_old_functions = 1;
#ifdef USE_GFREP_H
	else if (strcmp(&argv[0][1], "-force-gfrep-N") == 0)
		force_gfrep_N = 1;
#endif
	else if (strcmp(&argv[0][1], "-trace") == 0) {
		next_arg_set(&trace_name, argcp, argvp, 0);
		if (trace_name == NULL || (strcmp(trace_name, "-") == 0))
			enable_trace = stdout;
		else
			enable_trace = fopen(trace_name, "w");
		if (enable_trace == NULL) {
			perror(trace_name);
			exit(1);
		}
	} else if (strcmp(&argv[0][1], "-errlog") == 0) {
		int fd;
		next_arg_set(&errlog_name, argcp, argvp, 0);
		if (errlog_name == NULL) {
			gfarmfs_usage();
			exit(1);
		}
		fd = open(errlog_name, O_WRONLY|O_CREAT|O_APPEND, 0600);
		if (fd == -1) {
			fprintf(stderr, "logfile (open): %s: %s\n",
				errlog_name, strerror(errno));
			exit(1);
		}
		enable_errlog = fdopen(fd, "a");
		if (enable_errlog == NULL) {
			fprintf(stderr, "logfile (fdopen): %s: %s\n",
				errlog_name, strerror(errno));
			exit(1);
		}
		setvbuf(enable_errlog, NULL, _IOLBF, 0);
	} else if (strcmp(&argv[0][1], "-version") == 0) {
		gfarmfs_version(1);
		exit(0);
	} else {
		gfarmfs_usage();
		exit(1);
	}
}

static int
parse_short_option(int *argcp, char ***argvp)
{
	char **argv = *argvp;
	char *a = &argv[0][1];

	while (*a) {
		switch (*a) {
		case 's':
			enable_symlink = 1;
			break;
		case 'l':
			enable_linkiscopy = 1;
			break;
		case 'a':
			return (next_arg_set(&arch_name, argcp, argvp, 1));
		case 'u':
			enable_unlinkall = 1;
			break;
		case 'b':
			enable_gfarm_iobuf = 1;
			break;
		case 'F':
			enable_exact_filesize = 1;
			break;
#ifdef USE_GFS_STATFSNODE
		case 'S':
			enable_statfs = 0; /* disable */
			break;
#endif
		case 'm':
			return (next_arg_set(
					&gfarm_mount_point, argcp, argvp, 1));
		case 'n':
			enable_count_dir_nlink = 1;
			break;
#if ENABLE_ASYNC_REPLICATION
		case 'N':
			if (*(a + 1) == '\0') {
				char *num_str = NULL;
				next_arg_set(&num_str, argcp, argvp, 0);
				if (num_str == NULL)
					gfrep_target_num = -1;
				else
					gfrep_target_num = atoi(num_str);
			} else {
				long int i;
				char *endptr;
				i = strtol(a + 1, &endptr, 10);
				if (endptr == (a + 1))
					gfrep_target_num = -1;
				else {
					gfrep_target_num = i;
					a = endptr - 1;
					break;
				}
			}
			if (gfrep_target_num == -1) {
				gfrep_target_num = 2;
				break;
			} else
				return (0);
		case 'D':
			return (next_arg_set(&gfrep_dom, argcp, argvp, 1));
#endif
		case 'v':
			gfarmfs_version(1);
			exit(0);
		default:
			gfarmfs_usage();
			exit(1);
		}
		++a;
	}
	return (0);
}

static void
check_gfarmfs_options(int *argcp, char ***argvp)
{
	int argc = *argcp;
	char **argv = *argvp;
	char *argv0 = *argv;

	--argc;
	++argv;
	while (argc > 0 && argv[0][0] == '-') {
		if (argv[0][1] == '-')
			parse_long_option(&argc, &argv);
		else
			parse_short_option(&argc, &argv);
		--argc;
		++argv;
	}
	++argc;
	--argv;
	*argcp = argc;
	*argv = argv0;
	*argvp = argv;
}

static void
setup_options()
{
	char *e, *url;
	struct gfs_stat st;

#ifndef ENABLE_FASTCREATE  /* not defined */
	if (enable_fastcreate > 0)
		enable_fastcreate = -1; /* ignore on FUSE 2.5 */
#endif

	/* setup old I/O functions */
	if (use_old_functions == 1) {
		gfarmfs_oper_p = &gfarmfs_oper_base;
	}

	/* validate gfarm_mount_point */
	e = add_gfarm_prefix("/", &url);
	if (e == NULL) {
		e = gfs_stat(url, &st);
		if (e == NULL)
			gfs_stat_free(&st);
		else {
			fprintf(stderr, "%s: %s\n", gfarm_url2path(url), e);
			exit(1);
		}
		free(url);
	} else {
		fprintf(stderr, "add_gfarm_prefix: %s\n", e);
		exit(1);
	}

#if ENABLE_ASYNC_REPLICATION
	/* check for gfrep */
	if (gfrep_target_num == 0 || gfrep_target_num == 1) {
		enable_replication = REP_DISABLE;
	} else if (gfrep_target_num <= -1) { /* without -N option */
#ifdef USE_GFARM_SCRAMBLE
		/* use gfs_scramble_make_replica() in default */
		enable_replication = REP_SCRAMBLE;
#else
		enable_replication = REP_DISABLE;
#endif
	} else { /* gfrep_target_num >=2 */
#ifdef USE_GFREP_H
		if (force_gfrep_N)
			enable_replication = REP_GFREP_N;
		else
			enable_replication = REP_GFREP_H;
#else
		enable_replication = REP_GFREP_N;
#endif
	}
#endif
	/* checking the location of grep */
	if (enable_replication == REP_GFREP_N
#ifdef USE_GFREP_H
	    || enable_replication == REP_GFREP_H
#endif
		) {
		gfrep_path = commandpath("gfrep");
		if (gfrep_path == NULL) {
			fprintf(stderr,
				"gfrep: command not found (for -N option)\n");
			exit(1);
		}
	}

#ifdef USE_GFS_STATFSNODE
	/* -S, --disable-statfs */
	if (enable_statfs == 0)
		gfarmfs_oper_p->statfs = NULL; /* disable */
#endif

	if (enable_errlog != NULL) {
		errlog_out = enable_errlog;
		if (dup2(fileno(enable_errlog), fileno(stderr)) == -1) {
			fprintf(stderr, "dup2: %s\n", strerror(errno));
			exit(1);
		}
	} else {
		errlog_out = stderr;
	}
}

static void
print_options()
{
	if (gfarmfs_debug == 0)
		return;

	gfarmfs_version(0);
	if (enable_symlink == 1) {
		printf("enable symlink\n");
	}
	if (enable_linkiscopy == 1) {
		printf("enable linkiscopy\n");
	}
	if (arch_name != NULL) {
		printf("set architecture (%s)\n", arch_name);
	}
	if (enable_unlinkall == 1) {
		printf("enable unlinkall\n");
	}
#ifdef USE_GFS_STATFSNODE
	if (enable_statfs == 0) {
		printf("disable statfs\n");
	} else {
		printf("enable statfs\n");
	}
#else
	printf("disable statfs\n");
#endif
	if (enable_trace != NULL) {
		printf("enable trace (output file: %s)\n", trace_name);
	}
	if (enable_count_dir_nlink == 1) {
		printf("enable count_dir_nlink\n");
	}
	if (enable_gfarm_iobuf == 1) {
		printf("enable buffered I/O (unset GFARM_FILE_UNBUFFERED)\n");
	} else {
		printf("disable buffered I/O (set GFARM_FILE_UNBUFFERED)\n");
	}
	if (enable_exact_filesize == 1) {
		printf("enable exact filesize\n");
	}
	if (use_old_functions == 1) {
		printf("use old I/O functions\n");
	}
	if (*gfarm_mount_point != '\0') {
		printf("mountpoint: gfarm:%s\n", gfarm_mount_point);
	}
#if ENABLE_ASYNC_REPLICATION
	if (enable_replication == REP_GFREP_N
#ifdef USE_GFREP_H
	    || enable_replication == REP_GFREP_H
#endif
		) {
		if (gfrep_dom == NULL) {
			printf("enable '%s -N %d' to run automatically\n",
			       gfrep_path, gfrep_target_num);
		} else {
			printf("enable '%s -N %d -D %s' "
			       "to run automatically\n",
			       gfrep_path, gfrep_target_num, gfrep_dom);
		}
#ifdef USE_GFARM_SCRAMBLE
	} else if (enable_replication == REP_SCRAMBLE){
		printf("enable 'gfs_scramble_make_replica()' "
		       "to run automatically\n");
#endif
	} else {
		printf("disable replication\n");
	}
#endif/* ENABLE_ASYNC_REPLICATION */
}

int
main(int argc, char *argv[])
{
	int ret;
	char *e;
	char **newargv = NULL;
	int newargc = 0;

	if (argc > 0) {
		program_name = basename(argv[0]);
	}
	check_gfarmfs_options(&argc, &argv);
	check_fuse_options(&argc, &argv, &newargc, &newargv);

	e = gfarm_initialize(NULL, NULL);
	if (e != NULL) {
		fprintf(stderr, "gfarm_initialize: %s\n", e);
		exit(1);
	}
	setup_options();
	print_options();
#if ENABLE_ASYNC_REPLICATION
	e = gfarmfs_async_fork_count_initialize();
	if (e != NULL) {
		fprintf(stderr, "initialize: %s\n", e);
		exit(1);
	}
#endif
	ret = fuse_main(newargc, newargv, gfarmfs_oper_p);

	free(newargv);
	gfarmfs_fastcreate_check();
	if (enable_trace != NULL)
		fclose(enable_trace);
	if (enable_errlog != NULL)
		fclose(enable_errlog);
#if ENABLE_ASYNC_REPLICATION
	gfarmfs_async_fork_count_finalize();
#endif
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "gfarm_terminate: %s\n", e);
		exit(1);
	}

	return (ret);
}
