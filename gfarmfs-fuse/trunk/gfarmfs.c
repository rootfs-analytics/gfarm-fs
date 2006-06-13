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

  Copyright (c) 2005 National Institute of Advanced Industrial Science
  and Technology (AIST).  All Rights Reserved.
*/
#define GFARMFS_VER "1.3"
#define GFARMFS_VER_DATE "6 June 2006"

#if FUSE_USE_VERSION >= 25
/* #  warning FUSE 2.5 compatible mode. */
#elif FUSE_USE_VERSION == 22
/* #  warning FUSE 2.2 compatible mode. */
#define ENABLE_FASTCREATE
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
#include <sys/statfs.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <libgen.h>
#include <stdlib.h>
#include <time.h>

#include <gfarm/gfarm.h>

#ifndef GFS_DEV
#define GFS_DEV ((dev_t)-1);
#endif
#ifndef GFS_BLKSIZE
#define GFS_BLKSIZE 8192
#endif
#ifndef STAT_BLKSIZ
#define STAT_BLKSIZ 512
#endif

#define SYMLINK_MODE
#ifdef SYMLINK_MODE
#define SYMLINK_SUFFIX ".gfarmfs-symlink"
#endif

#define NOFLAGMENTINFO_AUTO_DELETE 0  /* 1: enable */

/* ################################################################### */

static int gfarmfs_debug = 0;  /* 1: error, 2: debug */
static int enable_symlink = 0;
static int enable_linkiscopy = 0;
static int enable_unlinkall = 0;
static int enable_fastcreate = 0;  /* can be used on FUSE version 2.2 only */
static int enable_gfarm_unbuf = 1; /* default: GFARM_FILE_UNBUFFERED */
static char *arch_name = NULL;
static int enable_count_dir_nlink = 1; /* default: enable */
static int enable_print_enoent = 0; /* default: do not print ENOENT */
static FILE *enable_trace = NULL;
static char *trace_out = "gfarmfs.trace"; /* default filename */
static int enable_statfs = 0;
static char **statfs_hosts = NULL;
static int statfs_nhosts;
static char *statfs_hosts_file = NULL;
static int enable_statisfstat = 0;  /* gfs_fstat instead of gfs_stat */

/* ################################################################### */

static inline char *
add_gfarm_prefix(const char *path)
{
	char *url;
	url = malloc(strlen(path) + 7);
	sprintf(url, "gfarm:%s", path);
	return (url);
}

#ifdef SYMLINK_MODE
static inline char *
add_gfarm_prefix_symlink_suffix(const char *path)
{
	char *url;
	url = malloc(strlen(path) + 7 + strlen(SYMLINK_SUFFIX));
	sprintf(url, "gfarm:%s%s", path, SYMLINK_SUFFIX);
	return (url);
}
#endif

#if 0
static char *
gfarmfs_init()
{
	return (NULL);
}
#else
#define gfarmfs_init() (NULL)
#endif

static inline int
gfarmfs_final(char *opname, char *e, int val_noerror, const char *name)
{
	if (e == NULL) {
		if (enable_trace != NULL) {
			fprintf(enable_trace, "OK: %s: %s\n", opname, name);
		}
		return (val_noerror);
	} else {
		if (enable_trace != NULL) {
			fprintf(enable_trace, "NG: %s: %s: %s\n",
				opname, name, e);
		}
		if (gfarmfs_debug >= 1) {
			if (enable_print_enoent == 0 &&
			    e == GFARM_ERR_NO_SUCH_OBJECT) {
				/* do not print */
			} else if (name != NULL) {
				fprintf(stderr, "%s: %s: %s\n",
					opname, name, e);
			} else {
				fprintf(stderr, "%s: %s\n", opname, e);
			}
		}
		return -gfarm_error_to_errno(e);
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

static char *
gfarmfs_check_program_and_set_arch_using_url(GFS_File gf, char *url)
{
	char *e;

	if (arch_name != NULL) {
		e = gfs_access(url, X_OK);
		if (e == NULL) {
			e = gfs_pio_set_view_section(gf, arch_name, NULL, 0);
			return (e);
		}
	}
	return gfs_pio_set_view_global(gf, 0);
}

static char *
gfarmfs_check_program_and_set_arch_using_mode(GFS_File gf, mode_t mode)
{
	char *e;

	if (arch_name != NULL && GFARM_S_IS_PROGRAM(mode)) {
		e = gfs_pio_set_view_section(gf, arch_name, NULL, 0);
		return (e);
	}
	return gfs_pio_set_view_global(gf, 0);
}

static char *
gfarmfs_create_empty_file(const char *path, mode_t mode)
{
	char *e, *e2;
	char *url;
	GFS_File gf;

	url = add_gfarm_prefix(path);
	e = gfs_pio_create(url,
			   GFARM_FILE_WRONLY|GFARM_FILE_TRUNC|
			   GFARM_FILE_EXCLUSIVE,
			   mode, &gf);
	if (e == NULL) {
		e2 = gfarmfs_check_program_and_set_arch_using_mode(gf, mode);
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
			printf("fastcreate flush: %s\n", fc.path);
		}
		e = gfarmfs_create_empty_file(fc.path, fc.mode);
		if (e != NULL) {
			if (gfarmfs_debug >= 1) {
				printf("fastcreate flush error: %s: %s\n",
				       fc.path, e);
			}
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
		printf("fastcreate add: %s\n", path);
	}
	return (NULL);
}

static char *
gfarmfs_fastcreate_open(const char *path, int flags, GFS_File *gfp)
{
	char *e;
	char *url;

	url = add_gfarm_prefix(path);
	if (fc.path != NULL && strcmp(fc.path, path) == 0) {
		if (gfarmfs_debug >= 2) {
			printf("fastcreate open: %s\n", path);
		}
		e = gfs_pio_create(url,
				   flags|GFARM_FILE_TRUNC|GFARM_FILE_EXCLUSIVE,
				   fc.mode, gfp);
		if (e == NULL) {
			e = gfarmfs_check_program_and_set_arch_using_mode(
				*gfp, fc.mode);
			if (e != NULL)
				gfs_pio_close(*gfp);
		}
		gfarmfs_fastcreate_free();
	} else {
		gfarmfs_fastcreate_flush(); /* flush previous saved path */
		e = gfs_pio_open(url, flags, gfp);
		if (e == NULL) {
			e = gfarmfs_check_program_and_set_arch_using_url(
				*gfp, url);
			if (e != NULL)
				gfs_pio_close(*gfp);
		}
	}
	free(url);
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
(enable_fastcreate == 1 ? (gfarmfs_fastcreate_flush() == NULL ? 1 : -1) : 0)

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

static int
gfarmfs_getattr(const char *path, struct stat *buf)
{
	struct gfs_stat gs;
	char *e;
	char *url;
	int symlinkmode = 0;

	if ((e = gfarmfs_init()) != NULL) goto end;

#ifdef ENABLE_FASTCREATE
	if (enable_fastcreate == 1 && gfarmfs_fastcreate_getattr(path, buf)) {
		goto end;
	}
#endif
	url = add_gfarm_prefix(path);
	e = gfs_stat(url, &gs);
#ifdef SYMLINK_MODE
	if (enable_symlink == 1 && e == GFARM_ERR_NO_SUCH_OBJECT) {
		free(url);
		url = add_gfarm_prefix_symlink_suffix(path);
		if (gfarmfs_debug >= 2) {
			printf("GETATTR: for symlink: %s\n", url);
		}
		e = gfs_stat(url, &gs);
		symlinkmode = 1;
	}
#endif
	while (enable_statisfstat == 1 && e == NULL &&
	       GFARM_S_ISREG(gs.st_mode)) {
		/* get st_size using gfs_fstat */
		/* On Gfarm version 1.3 (or earlier), gfs_stat cannot
		   get the correct st_size while a file is opened.
		   But gfs_fstat can do it.
		*/
		GFS_File gf;
		int flags, nf, i;
		char *e2;
		struct gfs_stat gs2;
		file_offset_t st_size;
		int change_mode = 0;
		mode_t save_mode = 0;

		if (gs.st_mode & 0400) {
			flags = GFARM_FILE_RDONLY;
#if 0
		} else if (gs.st_mode & 0200) {
			flags = GFARM_FILE_WRONLY;
#endif
		} else {
			save_mode = gs.st_mode;
			e2 = gfs_chmod(url, gs.st_mode|0400);
			if (e2 != NULL) {
				printf("GETATTR: cancel fstat: %s: %s\n",
				       path, e2);
				break; /* not my modifiable file */
			}
			change_mode = 1;
			flags = GFARM_FILE_RDONLY;
		}
		e2 = gfs_pio_open(url, flags, &gf);
		if (e2 != NULL) goto revert_mode;
		if (GFARM_S_IS_PROGRAM(gs.st_mode)) {
			if (arch_name == NULL) {
				/* ignore */
				/* e2 = gfs_pio_set_view_global(gf, 0); */
				goto fstat_close;
			} else {
				e2 = gfs_pio_set_view_section(
					gf, arch_name, NULL, 0);
			}
			if (e2 != NULL) goto fstat_close;
			e2 = gfs_fstat(gf, &gs2);
			if (e2 != NULL) goto fstat_close;
			gs.st_size = gs2.st_size;
			gfs_stat_free(&gs2);
		} else {
			e2 = gfs_pio_get_nfragment(gf, &nf);
			if (e2 != NULL) goto fstat_close;
			st_size = 0;
			for (i = 0; i < nf; i++) {
				e2 = gfs_pio_set_view_index(
					gf, nf, i, NULL, 0);
				if (e2 != NULL) goto fstat_close;
				e2 = gfs_fstat(gf, &gs2);
				if (e2 != NULL) goto fstat_close;
				st_size += gs2.st_size;
				gfs_stat_free(&gs2);
			}
			if (e2 == NULL) {
				gs.st_size = st_size;
			}
		}
	fstat_close:
		gfs_pio_close(gf);
		if (e2 != NULL && gfarmfs_debug > 0) {
			printf("GETATTR: fstat: %s: %s\n", path, e2);
		}
	revert_mode:
		if (change_mode == 1) {
			e2 = gfs_chmod(url, save_mode);
			if (e2 != NULL) {
				printf("GETATTR: revert st_mode...: %s: %s\n",
				       path, e2);
			}
		}
		break;
	}
#if NOFLAGMENTINFO_AUTO_DELETE == 1 /* to delete invalid path_info */
	if (e == NULL &&
	    !GFARM_S_ISDIR(gs.st_mode) &&
	    !GFARM_S_ISREG(gs.st_mode)) {
		struct gfarm_path_info pi;
		char *e2;
		char *p;
		printf("GETATTR: invalid entry: %s\n", path);
		e2 = gfarm_canonical_path(path, &p);
		if (e2 == NULL) {
			e2 = gfarm_path_info_get(p, &pi);
			if (e2 == NULL) {
				pi.status.st_mode = 0100600;
				e2 = gfarm_path_info_replace(p, &pi);
				printf("path_info_replace: %s\n", e2);
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
		url = add_gfarm_prefix(path);
		e = gfs_opendir(url, &dir);
		free(url);
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
		if (enable_fastcreate == 1) {
			e = gfarmfs_fastcreate_save(path, mode);
		} else {
			e = gfarmfs_create_empty_file(path, mode);
		}
#else
		e = gfarmfs_create_empty_file(path, mode);
#endif
	} else {
		if (gfarmfs_debug >= 1) {
			printf("MKNOD: not supported: mode = %o, rdev = %o\n",
			       mode, (int)rdev);
		}
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
		url = add_gfarm_prefix(path);
		e = gfs_mkdir(url, mode);
		free(url);
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
		url = add_gfarm_prefix(path);
		e = gfs_unlink(url);
		free(url);
#ifdef SYMLINK_MODE
		if (enable_symlink == 1 && e == GFARM_ERR_NO_SUCH_OBJECT) {
			url = add_gfarm_prefix_symlink_suffix(path);
			if (gfarmfs_debug >= 2) {
				printf("UNLINK: for symlink: %s\n", path);
			}
			e = gfs_unlink(url);
			free(url);
		}
#endif
	} else {  /* remove a self architecture file */
		struct gfs_stat gs;

		url = add_gfarm_prefix(path);
		e = gfs_stat(url, &gs);
#ifdef SYMLINK_MODE
		if (enable_symlink == 1 && e == GFARM_ERR_NO_SUCH_OBJECT) {
			free(url);
			url = add_gfarm_prefix_symlink_suffix(path);
			if (gfarmfs_debug >= 2) {
				printf("UNLINK: for symlink: %s\n", path);
			}
			e = gfs_stat(url, &gs);
		}
#endif
		if (e == NULL) {    /* gfs_stat succeeds */
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
		url = add_gfarm_prefix(path);
		e = gfs_rmdir(url);
		free(url);
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
	url = add_gfarm_prefix_symlink_suffix(path);
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
	url = add_gfarm_prefix_symlink_suffix(to);

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
		from_url = add_gfarm_prefix(from);
		to_url = add_gfarm_prefix(to);
		e = gfs_rename(from_url, to_url);
#ifdef SYMLINK_MODE
		if (enable_symlink == 1 && e == GFARM_ERR_NO_SUCH_OBJECT) {
			free(from_url);
			free(to_url);
			from_url = add_gfarm_prefix_symlink_suffix(from);
			to_url = add_gfarm_prefix_symlink_suffix(to);
			if (gfarmfs_debug >= 2) {
				printf("RENAME: for symlink: %s\n", from);
			}
			e = gfs_rename(from_url, to_url);
		}
#endif
		free(from_url);
		free(to_url);
	}

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
	from_url = add_gfarm_prefix(from);
	/* need to feee from_url [1] */
	e = gfs_stat(from_url, &gs);
#ifdef SYMLINK_MODE
	if (enable_symlink == 1 && e == GFARM_ERR_NO_SUCH_OBJECT) {
		free(from_url);
		from_url = add_gfarm_prefix_symlink_suffix(from);
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

	e = gfarmfs_check_program_and_set_arch_using_url(from_gf, from_url);
	if (e != NULL) goto close_from_gf;

#ifdef SYMLINK_MODE
	if (symlinkmode == 1) {
		to_url = add_gfarm_prefix_symlink_suffix(to);
	} else {
		to_url = add_gfarm_prefix(to);
	}
#else
	to_url = add_gfarm_prefix(to);
#endif
	/* need to free to_url [5] */

	e = gfs_pio_create(to_url,
			   GFARM_FILE_WRONLY|GFARM_FILE_TRUNC|
			   GFARM_FILE_EXCLUSIVE,
			   gs.st_mode, &to_gf);
	if (e != NULL) goto free_to_url;
	/* need to close to_gf [6] */

	e = gfarmfs_check_program_and_set_arch_using_mode(to_gf, gs.st_mode);
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
		url = add_gfarm_prefix(path);
		e = gfs_chmod(url, mode);
		free(url);
	}

	return gfarmfs_final("CHMOD", e, 0, path);
}

static int
gfarmfs_chown(const char *path, uid_t uid, gid_t gid)
{
	char *e;
	char *url;
	struct gfs_stat s;

	gfarmfs_fastcreate_check();
	e = gfarmfs_init();
	if (e == NULL) {
		url = add_gfarm_prefix(path);
		e = gfs_stat(url, &s);
		free(url);
#ifdef SYMLINK_MODE
		if (enable_symlink == 1 && e == GFARM_ERR_NO_SUCH_OBJECT) {
			url = add_gfarm_prefix_symlink_suffix(path);
			if (gfarmfs_debug >= 2) {
				printf("CHOWN: for symlink: %s\n", url);
			}
			e = gfs_stat(url, &s);
			free(url);
		}
#endif
		if (e == NULL) {
			if (strcmp(s.st_user, gfarm_get_global_username())
			    != 0) {
				/* EPERM */
				e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			}
			/* XXX - do nothing */
			gfs_stat_free(&s);
		}
	}

	return gfarmfs_final("CHOWN", e, 0, path);
}

static int
gfarmfs_truncate(const char *path, off_t size)
{
	char *e;
	GFS_File gf;
	char *url;

	gfarmfs_fastcreate_check();
	url = add_gfarm_prefix(path);
	e = gfarmfs_init();
	while (e == NULL) {
		e = gfs_pio_open(url, GFARM_FILE_WRONLY, &gf);
		if (e != NULL) break;

		e = gfarmfs_check_program_and_set_arch_using_url(gf, url);
		if (e == NULL) {
			e = gfs_pio_truncate(gf, size);
		}
		gfs_pio_close(gf);
		break;
	}
	free(url);

	return gfarmfs_final("TRUNCATE", e, 0, path);
}

static int
gfarmfs_utime(const char *path, struct utimbuf *buf)
{
	char *e;
	char *url;

	gfarmfs_fastcreate_check();
	url = add_gfarm_prefix(path);
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
	while (e == NULL) {
		if ((fi->flags & O_ACCMODE) == O_RDONLY) {
			flags = GFARM_FILE_RDONLY;
		} else if ((fi->flags & O_ACCMODE) == O_WRONLY) {
			flags = GFARM_FILE_WRONLY;
		} else if ((fi->flags & O_ACCMODE) == O_RDWR) {
			flags = GFARM_FILE_RDWR;
		}
		if (enable_gfarm_unbuf == 1) {
			flags |= GFARM_FILE_UNBUFFERED;
		}

#ifdef ENABLE_FASTCREATE
		if (enable_fastcreate == 1) {
			/* check a created file on memory and create/open */
			/* with checking program */
			e = gfarmfs_fastcreate_open(path, flags, &gf);
			if (e != NULL) {
				break;
			}
		} else {
#endif
			url = add_gfarm_prefix(path);
			e = gfs_pio_open(url, flags, &gf);
			if (e != NULL) {
				free(url);
				break;
			}
			e = gfarmfs_check_program_and_set_arch_using_url(
				gf, url);
			free(url);
			if (e != NULL) {
				gfs_pio_close(gf);
				break;
			}
#ifdef ENABLE_FASTCREATE
		}
#endif
		fi->fh = (unsigned long) gf;
		break;
	}

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
		if (enable_gfarm_unbuf == 1) {
			flags |= GFARM_FILE_UNBUFFERED;
		}

		url = add_gfarm_prefix(path);
		e = gfs_pio_create(url,
				   flags|GFARM_FILE_TRUNC|GFARM_FILE_EXCLUSIVE,
				   mode, &gf);
		if (e != NULL) {
			free(url);
			break;
		}
		e = gfarmfs_check_program_and_set_arch_using_url(gf, url);
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
		url = add_gfarm_prefix(path);
		e = gfs_access(url, mask);
		free(url);
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

	return gfarmfs_final("RELEASE", e, 0, path);
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
	int retry, char *hostname,
	gfarm_int32_t *bsizep,
	file_offset_t *blocksp, file_offset_t *bfreep, file_offset_t *bavailp,
	file_offset_t *filesp, file_offset_t *ffreep, file_offset_t *favailp)
{
	char *e;

	e = gfs_statfsnode_cached(hostname,
				  bsizep, blocksp, bfreep, bavailp,
				  filesp, ffreep, favailp);
	if (e == GFARM_ERR_NO_SUCH_OBJECT && retry == 1) {
		e = gfs_statfsnode(hostname,
				  bsizep, blocksp, bfreep, bavailp,
				  filesp, ffreep, favailp);
	}
	if (e != NULL) {
		if (gfarmfs_debug > 0) {
			printf("ERROR: STATFS: %s: %s\n", hostname, e);
		}
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
	static int retry = 1;

	*bsizep = *blocksp = *bfreep = *bavailp
		= *filesp = *ffreep = *favailp = 0;
	ok = 0;
	for (i = 0; i < nhosts; i++) {
		e = gfs_statfsnode_cached_and_retry(retry, hosts[i], &bsize,
						    &blocks, &bfree,
						    &bavail, &files,
						    &ffree, &favail);
		if (e == NULL) {
			if (*bsizep == 0) {
				*bsizep = bsize;
			} else if (*bsizep != bsize) {
				/* XXX - consider bsize (re-calculate?) */
				continue;
			}
			ok = 1;
			*blocksp += blocks;
			*bfreep  += bfree;
			*bavailp += bavail;
			*filesp  += files;
			*ffreep  += ffree;
			*favailp += favail;
		} else {
			e_save = e;
		}
	}
	retry = 0;
	if (ok == 1) {
		return (NULL);
	} else {
		return (e_save);
	}
}

#if FUSE_USE_VERSION == 22
static int
gfarmfs_statfs(const char *path, struct statfs *stfs)
{
	char *e;
	file_offset_t favail;

	if ((e = gfarmfs_init()) != NULL) goto end;

	e = gfs_statfsnode_total_of_hostlist(statfs_hosts, statfs_nhosts,
					     (gfarm_int32_t*)&stfs->f_bsize,
					     (file_offset_t*)&stfs->f_blocks,
					     (file_offset_t*)&stfs->f_bfree,
					     (file_offset_t*)&stfs->f_bavail,
					     (file_offset_t*)&stfs->f_files,
					     (file_offset_t*)&stfs->f_ffree,
					     (file_offset_t*)&favail);

end:
	return gfarmfs_final("STATFS", e, 0, path);
}
#elif FUSE_USE_VERSION >= 25
static int
gfarmfs_statfs(
	const char *path, struct statvfs *stvfs)
{
	char *e;

	if ((e = gfarmfs_init()) != NULL) goto end;

	e = gfs_statfsnode_total_of_hostlist(statfs_hosts, statfs_nhosts,
					     (gfarm_int32_t*)&stvfs->f_bsize,
					     (file_offset_t*)&stvfs->f_blocks,
					     (file_offset_t*)&stvfs->f_bfree,
					     (file_offset_t*)&stvfs->f_bavail,
					     (file_offset_t*)&stvfs->f_files,
					     (file_offset_t*)&stvfs->f_ffree,
					     (file_offset_t*)&stvfs->f_favail);
end:
	return gfarmfs_final("STATFS", e, 0, path);
}
#endif /* FUSE_USE_VERSION */
#endif /* USE_GFS_STATFSNODE */

#if 1
static int
gfarmfs_flush(const char *path, struct fuse_file_info *fi)
{
	char *e = gfarmfs_init();
	return gfarmfs_final("(FLUSH)", e, 0, path);
}
#endif

#if 0
/* #ifdef HAVE_SETXATTR */
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
#endif /* HAVE_SETXATTR */

static struct fuse_operations gfarmfs_oper = {
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
#if 1
	.flush     = gfarmfs_flush,
#endif
#if 0
/* #ifdef HAVE_SETXATTR */
	.setxattr    = gfarmfs_setxattr,
	.getxattr    = gfarmfs_getxattr,
	.listxattr   = gfarmfs_listxattr,
	.removexattr = gfarmfs_removexattr,
#endif
};

/* ################################################################### */

static char *program_name = "gfarmfs";

static void
gfarmfs_version()
{
	printf("GfarmFS-FUSE version %s (%s)\n",
	       GFARMFS_VER, GFARMFS_VER_DATE);
	printf("Build: %s %s\n", __DATE__, __TIME__);
#if FUSE_USE_VERSION >= 25
	printf("FUSE version 2.5 compatible mode\n");
	printf("Recommend GfarmFS options:\n");
	printf("    -s -u --statfs --statisfstat\n");
	printf("Recommend FUSE options:\n");
	printf("    -o default_permissions\n");
#else
	printf("FUSE version 2.2 compatible mode\n");
	printf("Recommend GfarmFS options:\n");
	printf("    -f -s -u --statfs --statisfstat\n");
	printf("Recommend FUSE options:\n");
	printf("    -o default_permissions\n");
#endif
}

static void
gfarmfs_usage()
{
	const char *fusehelp[] = { program_name, "-ho", NULL };

	fprintf(stderr,
"Usage: %s [GfarmFS options] <mountpoint> [FUSE options]\n"
"\n"
"GfarmFS options:\n"
#ifdef ENABLE_FASTCREATE
"    -f, --fastcreate       improve performance of file creation\n"
"                           (MKNOD does not flush the meta data)\n"
#endif
#ifdef SYMLINK_MODE
"    -s, --symlink          enable symlink(2) to work (emulation)\n"
#endif
"    -l, --linkiscopy       enable link(2) to behave copying a file (emulation)\n"
"    -u, --unlinkall        enable unlink(2) to remove all architecture files\n"
#ifdef USE_GFS_STATFSNODE
"    -S, --statfs           enable statfs(2) (total of hosts from gfhost)\n"
"    -H <hostfile>          enable statfs(2) (total of hosts in hostfile)\n"
#endif
"    --buffered             enable buffered I/O (unset GFARM_FILE_UNBUFFERED)\n"
"    -a <architecture>      for a client not registered by gfhost\n"
"    --trace <filename>     record FUSE operations called by processes\n"
"    --print-enoent         do not ignore to print ENOENT to stderr\n"
"                           (in -f or -d of FUSE option) (default: ignore)\n"
"    -v, --version          show version and exit\n"
"\n"
#ifdef ENABLE_FASTCREATE
"    --safe                 equivalent to -fsS --unbuffered --statisfstat\n"
"    --fast                 equivalent to -f --buffered --disable-dirnlink\n"
#else
"    --safe                 equivalent to -sS --unbuffered --statisfstat\n"
"    --fast                 equivalent to --buffered --disable-dirnlink\n"
#endif
"\n", program_name);

	fuse_main(2, (char **) fusehelp, &gfarmfs_oper);
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
	else if (strcmp(&argv[0][1], "-fastcreate") == 0)
		enable_fastcreate = 1;
	else if (strcmp(&argv[0][1], "-unbuffered") == 0)
		enable_gfarm_unbuf = 1;
	else if (strcmp(&argv[0][1], "-buffered") == 0)
		enable_gfarm_unbuf = 0;
	else if (strcmp(&argv[0][1], "-statfs") == 0)
		enable_statfs = 1;
	else if (strcmp(&argv[0][1], "-print-enoent") == 0)
		enable_print_enoent = 1;
	else if (strcmp(&argv[0][1], "-trace") == 0) {
		next_arg_set(&trace_out, argcp, argvp, 0);
		if (trace_out == NULL || (strcmp(trace_out, "-") == 0))
			enable_trace = stdout;
		else
			enable_trace = fopen(trace_out, "w");
		if (enable_trace == NULL) {
			perror(trace_out);
			exit(1);
		}
	} else if (strcmp(&argv[0][1], "-version") == 0) {
		gfarmfs_version();
		exit(0);
	} else if (strcmp(&argv[0][1], "-disable-dirnlink") == 0)
		enable_count_dir_nlink = 0;
	else if (strcmp(&argv[0][1], "-statisfstat") == 0)
		/* On Gfarm version 1.3 (or earlier), gfs_stat
		   cannot get the correct st_size while a file
		   is opened.  But gfs_fstat can do it.
		*/
		enable_statisfstat = 1;
	else if (strcmp(&argv[0][1], "-safe") == 0) {
		enable_symlink = 1;     /* -s */
#ifdef ENABLE_FASTCREATE
		enable_fastcreate = 1;  /* -f */
#endif
		enable_gfarm_unbuf = 1; /* --unbuffered */
		enable_statisfstat = 1; /* --statfsisfstat */
		enable_statfs = 1;      /* -S */
	} else if (strcmp(&argv[0][1], "-fast") == 0) {
#ifdef ENABLE_FASTCREATE
		enable_fastcreate = 1;  /* -f */
#endif
		enable_count_dir_nlink = 0; /* --disable-dirnlink */
		enable_gfarm_unbuf = 0; /* --buffered */
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
		case 'f':
			enable_fastcreate = 1;
			break;
		case 'S':
			enable_statfs = 1;
			break;
		case 'H':
			enable_statfs = 1;
			return (next_arg_set(
					&statfs_hosts_file, argcp, argvp, 1));
		case 'v':
			gfarmfs_version();
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
	char *e;
	int lineno;

	if (enable_statfs == 1) {
		if (statfs_hosts_file != NULL) {
			e = gfarm_hostlist_read(statfs_hosts_file,
						&statfs_nhosts, &statfs_hosts,
						&lineno);
			if (e != NULL) {
				if (lineno != -1) {
					fprintf(stderr,
						"%s: line %d: %s\n",
						statfs_hosts_file, lineno, e);
				} else {
					fprintf(stderr, "%s: %s\n",
						statfs_hosts_file, e);
				}
				exit(1);
			}
		} else {
			char buf[256];  /* XXX - MAXHOSTNAMELEN */
			FILE *pin;
			char *p;
			gfarm_stringlist list;

			e = gfarm_stringlist_init(&list);
			if (e != NULL) {
				fprintf(stderr, "stringlist_init: %s\n", e);
				exit(1);
			}
			pin = popen("gfhost", "r");
			if (pin == NULL) {
				gfarm_stringlist_free(&list);
				perror("popen(gfhost)");
				exit(1);
			}
			while (fgets(buf, 256, pin) != NULL) {
				p = strchr(buf, '\n');
				if (p != NULL)
					*p = '\0';
				p = strdup(buf);
				e = gfarm_stringlist_add(&list, p);
				if (e != NULL) {
					free(p);
					gfarm_stringlist_free_deeply(&list);
					fprintf(stderr, "gfhost: %s\n", e);
					exit(1);
				}
			}
			pclose(pin);
			statfs_nhosts = gfarm_stringlist_length(&list);
			statfs_hosts =
				gfarm_strings_alloc_from_stringlist(&list);
			gfarm_stringlist_free(&list);
			if (statfs_nhosts == 0 || statfs_hosts == NULL) {
				enable_statfs = 0;
				gfarmfs_oper.statfs = NULL;
			}
		}
	} else {
		gfarmfs_oper.statfs = NULL;
	}
}

static void
print_options()
{
	if (gfarmfs_debug > 0) {
		gfarmfs_version();
		if (enable_symlink == 1) {
			printf("enable symlink\n");
		}
		if (enable_linkiscopy == 1) {
			printf("enable linkiscopy\n");
		}
		if (arch_name != NULL) {
			printf("set architecture: %s\n", arch_name);
		}
		if (enable_unlinkall == 1) {
			printf("enable unlinkall\n");
		}
		if (enable_fastcreate == 1) {
#ifdef ENABLE_FASTCREATE
			printf("enable fastcreate\n");
#else
			printf("fastcreate is not needed by FUSE 2.5 compatible mode. (disabled)\n");
#endif
		}
		if (enable_statfs == 1) {
			printf("enable statfs: %d hosts\n",
			       statfs_nhosts);
		}
		if (statfs_hosts_file != NULL) {
			if (strcmp(statfs_hosts_file, "-") == 0) {
				printf("statfs: hostlist: stdin\n");
			} else {
				printf("statfs: hostlist: %s\n",
				       statfs_hosts_file);
			}
		}
		if (enable_trace != NULL) {
			printf("enable trace: output file: %s\n", trace_out);
		}

		/* hidden options */
		if (enable_count_dir_nlink == 0) {
			printf("disable count_dir_nlink\n");
		}
		if (enable_statisfstat == 1) {
			printf("correct st_size during file-opened (gfs_fstat instead of gfs_stat)\n");
			if (enable_gfarm_unbuf == 0) {
				printf("warning: not set GFARM_FILE_UNBUFFERED\n");
			}
		}
#ifndef ENABLE_FASTCREATE
#endif
	}
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
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(-1);
	}
	setup_options();
	print_options();

	ret = fuse_main(newargc, newargv, &gfarmfs_oper);
	free(newargv);

	gfarmfs_fastcreate_check();

	if (enable_trace != NULL) {
		fclose(enable_trace);
	}

	return (ret);
}
