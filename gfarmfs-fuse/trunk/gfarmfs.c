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

#define GFARMFS_VER "1.2.1"
#define GFARMFS_VER_DATE "1 Feb 2006"

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

/* ################################################################### */

static int gfarmfs_debug = 0;  /* 1: error, 2: debug */
static int enable_symlink = 0;
static int enable_linkiscopy = 0;
static int enable_unlinkall = 0;
static int enable_fastcreate = 0;
static int enable_gfarm_unbuf = 0;
static char *arch_name = NULL;

/* This is necessary to free the memory space by free(). */
static char *
add_gfarm_prefix(const char *path)
{
	char *url;
	url = malloc(strlen(path) + 7);
	sprintf(url, "gfarm:%s", path);
	return (url);
}

#ifdef SYMLINK_MODE
/* This is necessary to free the memory space by free(). */
static char *
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

static int
gfarmfs_final(char *e, int val_noerror, const char *name)
{
	if (e == NULL) {
		return (val_noerror);
	} else {
		if (gfarmfs_debug >= 1) {
			if (name != NULL) {
				fprintf(stderr, "error: %s: %s\n", name, e);
			} else {
				fprintf(stderr, "error: %s\n", e);
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
	return (NULL);
}

static char *
gfarmfs_check_program_and_set_arch_using_mode(GFS_File gf, mode_t mode)
{
	char *e;

	if (arch_name != NULL &&
	    (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0) {
		e = gfs_pio_set_view_section(gf, arch_name, NULL, 0);
		return (e);
	} else {
		return (NULL);
	}
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
		}
		gfarmfs_fastcreate_free();
	} else {
		gfarmfs_fastcreate_flush(); /* flush previous saved path */
		e = gfs_pio_open(url, flags, gfp);
		if (e == NULL) {
			e = gfarmfs_check_program_and_set_arch_using_url(
				*gfp, url);
		}
	}
	free(url);
	return (e);
}

static int
gfarmfs_fastcreate_getattr(const char *path, struct stat *buf)
{
	time_t now;

	if(fc.path != NULL && strcmp(fc.path, path) == 0) {
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
		buf->st_atime =
			buf->st_mtime =
			buf->st_ctime = now;
		return (1);
	} else {
		return (0);
	}
}

#define gfarmfs_fastcreate_check() \
(enable_fastcreate == 1 ? (gfarmfs_fastcreate_flush() == NULL ? 1 : -1) : 0)

/* ################################################################### */

static int
gfarmfs_getattr(const char *path, struct stat *buf)
{
	/* referred to hooks_stat.c */
	struct gfs_stat gs;
	char *e;
	struct passwd *p;
	char *url;
#ifdef SYMLINK_MODE
	int symlinkmode = 0;
#endif
	static struct passwd *p_save = NULL;
	static char *username_save = NULL;

	if (enable_fastcreate == 1 && gfarmfs_fastcreate_getattr(path, buf)) {
		return (0);
	}

	url = add_gfarm_prefix(path);
	e = gfarmfs_init();
	if (e == NULL) {
		e = gfs_stat(url, &gs);
#ifdef SYMLINK_MODE
		if (enable_symlink == 1 && e != NULL) {
			if (gfarm_error_to_errno(e) == ENOENT) {
				free(url);
				url = add_gfarm_prefix_symlink_suffix(path);
				if (gfarmfs_debug >= 2) {
					printf("check for symlink: %s\n", url);
				}
				e = gfs_stat(url, &gs);
				symlinkmode = 1;
			}
		}
#endif
	}
	if (e == NULL) {
		memset(buf, 0, sizeof(struct stat));
		buf->st_dev = GFS_DEV;
		buf->st_ino = gs.st_ino;
		buf->st_mode = gs.st_mode;
#ifdef SYMLINK_MODE
		if (symlinkmode == 1) {
			buf->st_mode = 0777 | S_IFLNK;
		}
#endif
		if (GFARM_S_ISDIR(buf->st_mode)) {
			buf->st_nlink = gfarmfs_dir_nlink(url);
		} else {
			buf->st_nlink = 1;
		}

		if (gs.st_user != NULL && username_save != NULL &&
		    strcmp(gs.st_user, username_save) == 0) {
			buf->st_uid = p_save->pw_uid;
			buf->st_gid = p_save->pw_gid;
			if (gfarmfs_debug >= 2) {
				printf("getpwnam cancel: use cache\n");
			}
		} else if (gs.st_user != NULL &&
			   ((p = getpwnam(gs.st_user)) != NULL)) {
			buf->st_uid = p->pw_uid;
			buf->st_gid = p->pw_gid;
			if (username_save != NULL)
				free(username_save);
			username_save = strdup(gs.st_user);
			p_save = p;
		} else {
			buf->st_uid = getuid();
			buf->st_gid = getgid();
		}

		buf->st_size = gs.st_size;
		buf->st_blksize = GFS_BLKSIZE;
		buf->st_blocks = (gs.st_size + STAT_BLKSIZ - 1) / STAT_BLKSIZ;
		buf->st_atime = gs.st_atimespec.tv_sec;
		buf->st_mtime = gs.st_mtimespec.tv_sec;
		buf->st_ctime = gs.st_ctimespec.tv_sec;
		gfs_stat_free(&gs);
	}
	free(url);

	return gfarmfs_final(e, 0, path);
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
	return gfarmfs_final(e, 0, path);
}

static int
gfarmfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	char *e;

	if (rdev != 0 && ((rdev & S_IFREG) != S_IFREG)) {
		if (gfarmfs_debug >= 1) {
			printf("mknod: not supported: rdev = %d\n", (int)rdev);
		}
		return (-ENOSYS);  /* XXX */
	}

	if (enable_fastcreate == 1) {
		e = gfarmfs_fastcreate_save(path, mode);
	} else {
		e = gfarmfs_create_empty_file(path, mode);
	}
	return gfarmfs_final(e, 0, path);
}

static int
gfarmfs_mkdir(const char *path, mode_t mode)
{
	char *e;
	char *url;

	gfarmfs_fastcreate_check();
	url = add_gfarm_prefix(path);
	e = gfarmfs_init();
	if (e == NULL) {
		e = gfs_mkdir(url, mode);
	}
	free(url);

	return gfarmfs_final(e, 0, path);
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
		if (enable_symlink == 1 && e != NULL &&
		    gfarm_error_to_errno(e) == ENOENT) {
			url = add_gfarm_prefix_symlink_suffix(path);
			if (gfarmfs_debug >= 2) {
				printf("unlink: for symlink: %s\n", path);
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
		if (enable_symlink == 1 && e != NULL &&
		    gfarm_error_to_errno(e) == ENOENT) {
			free(url);
			url = add_gfarm_prefix_symlink_suffix(path);
			if (gfarmfs_debug >= 2) {
				printf("unlink: for symlink: %s\n", path);
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
	return gfarmfs_final(e, 0, path);
}

static int
gfarmfs_rmdir(const char *path)
{
	char *e;
	char *url;

	gfarmfs_fastcreate_check();
	url = add_gfarm_prefix(path);
	e = gfarmfs_init();
	if (e == NULL) {
		e = gfs_rmdir(url);
	}
	free(url);

	return gfarmfs_final(e, 0, path);
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
	if (enable_symlink == 0) {
		return (-ENOSYS);
	}

	url = add_gfarm_prefix_symlink_suffix(path);
	e = gfarmfs_init();
	while (e == NULL) {
		e = gfs_pio_open(url, GFARM_FILE_RDONLY, &gf);
		if (e != NULL) break;
		e = gfs_pio_read(gf, buf, size - 1, &n);
		gfs_pio_close(gf);
		break;
	}
	free(url);

	buf[n] = '\0';

	return gfarmfs_final(e, 0, path);
#else
	gfarmfs_fastcreate_check();
	return (-ENOSYS);
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
	if (enable_symlink == 0) {
		return (-ENOSYS);
	}
	url = add_gfarm_prefix_symlink_suffix(to);
	e = gfarmfs_init();
	while (e == NULL) {
		e = gfs_pio_create(url,
				   GFARM_FILE_WRONLY|GFARM_FILE_TRUNC|
				   GFARM_FILE_EXCLUSIVE,
				   0644, &gf);
		if (e != NULL) break;
		len = strlen(from);
		e = gfs_pio_write(gf, from, len, &n);
		gfs_pio_close(gf);
		if (len != n) {
			e = gfs_unlink(url);
			gfarmfs_final(e, 0, to);
			free(url);
			return (-EIO);
		}
		break;
	}
	free(url);

	return gfarmfs_final(e, 0, to);
#else
	gfarmfs_fastcreate_check();
	return (-ENOSYS);
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
		if (enable_symlink == 1 && e != NULL) {
			if (gfarm_error_to_errno(e) == ENOENT) {
				free(from_url);
				free(to_url);
				from_url =
					add_gfarm_prefix_symlink_suffix(from);
				to_url = add_gfarm_prefix_symlink_suffix(to);
				if (gfarmfs_debug >= 2) {
					printf("rename: for symlink: %s\n",
					       from);
				}
				e = gfs_rename(from_url, to_url);
			}
		}
#endif
		free(from_url);
		free(to_url);
	}
	return gfarmfs_final(e, 0, to);
}

static int
gfarmfs_link(const char *from, const char *to)
{
	char *e;
	char *from_url, *to_url;
	GFS_File from_gf, to_gf;
	int from_opened, to_opened;
	struct gfs_stat gs;
	int mode;
	int m, n;
	char buf[4096];
	int symlinkmode = 0;

	gfarmfs_fastcreate_check();
	if (enable_linkiscopy == 0) {
		return (-ENOSYS);
	}

	if (gfarmfs_debug >= 2) {
		printf("hard link is replaced by copy: %s\n", to);
	}
	from_opened = to_opened = 0;
	from_url = add_gfarm_prefix(from);
	e = gfarmfs_init();
	while (e == NULL) {
		e = gfs_stat(from_url, &gs);
#ifdef SYMLINK_MODE
		if (enable_symlink == 1 && e != NULL) {
			if (gfarm_error_to_errno(e) == ENOENT) {
				free(from_url);
				from_url =
					add_gfarm_prefix_symlink_suffix(from);
				if (gfarmfs_debug >= 2) {
					printf("link: for symlink: %s\n",
					       from);
				}
				e = gfs_stat(from_url, &gs);
				symlinkmode = 1;
			}
		}
#endif
		if (e != NULL) break;
		mode = gs.st_mode;
		gfs_stat_free(&gs);

		e = gfs_pio_open(from_url, GFARM_FILE_RDONLY, &from_gf);
		if (e != NULL) break;
		from_opened = 1;

		e = gfarmfs_check_program_and_set_arch_using_url(from_gf,
								 from_url);
		if (e != NULL) break;

		if (symlinkmode == 1) {
			to_url = add_gfarm_prefix_symlink_suffix(to);
		} else {
			to_url = add_gfarm_prefix(to);
		}
		e = gfs_pio_create(to_url,
				   GFARM_FILE_WRONLY|GFARM_FILE_TRUNC|
				   GFARM_FILE_EXCLUSIVE,
				   mode, &to_gf);
		free(to_url);
		if (e != NULL) break;
		to_opened = 1;

		e = gfarmfs_check_program_and_set_arch_using_mode(to_gf, mode);
		if (e != NULL) break;

		while(1) {
			e = gfs_pio_read(from_gf, buf, 4096, &m);
			if (e != NULL) break;
			if (m == 0) { /* EOF */
				break;
			}
			e = gfs_pio_write(to_gf, buf, m, &n);
			if (e != NULL) break;
			if (m != n) {
				e = GFARM_ERR_INPUT_OUTPUT;
				break;
			}
		}
		break;
	}
	free(from_url);

	if (from_opened == 1) {
		gfs_pio_close(from_gf);
	}
	if (to_opened == 1) {
		gfs_pio_close(to_gf);
	}
	return gfarmfs_final(e, 0, to);
}

static int
gfarmfs_chmod(const char *path, mode_t mode)
{
	char *e;
	char *url;

	gfarmfs_fastcreate_check();
	url = add_gfarm_prefix(path);
	e = gfarmfs_init();
	if (e == NULL) {
		e = gfs_chmod(url, mode);
	}
	free(url);

	return gfarmfs_final(e, 0, path);
}

static int
gfarmfs_chown(const char *path, uid_t uid, gid_t gid)
{
	/* referred to hooks.c */
	char *e;
	char *url;
	struct gfs_stat s;

	gfarmfs_fastcreate_check();
	e = gfarmfs_init();
	if (e == NULL) {
		url = add_gfarm_prefix(path);
		e = gfs_stat(url, &s);
		free(url);
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
	return gfarmfs_final(e, 0, path);
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

	return gfarmfs_final(e, 0, path);
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

	return gfarmfs_final(e, 0, path);
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

		if (enable_fastcreate == 1) {
			/* check a created file on memory and create/open */
			/* with checking program */
			e = gfarmfs_fastcreate_open(path, flags, &gf);
			if (e != NULL) {
				break;
			}
		} else {
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
		}
		fi->fh = (unsigned long) gf;
		break;
	}

	return gfarmfs_final(e, 0, path);
}

static int
gfarmfs_release(const char *path, struct fuse_file_info *fi)
{
	char *e;
	GFS_File gf;

	gfarmfs_fastcreate_check();
	e = gfarmfs_init();
	if (e == NULL) {
		gf = (GFS_File) fi->fh;
		e = gfs_pio_close(gf);
	}

	return gfarmfs_final(e, 0, path);
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
		gf = (GFS_File) fi->fh;
		e = gfs_pio_seek(gf, offset, 0, &off);
		if (e != NULL) break;
		e = gfs_pio_read(gf, buf, size, &n);
		break;
	}

	return gfarmfs_final(e, n, path);
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
		gf = (GFS_File) fi->fh;
		e = gfs_pio_seek(gf, offset, 0, &off);
		if (e != NULL) break;
		e = gfs_pio_write(gf, buf, size, &n);
		break;
	}

	return gfarmfs_final(e, n, path);
}

#if 0
static int
gfarmfs_statfs(const char *path, struct statfs *stbuf)
{
	return (-ENOSYS);
}

static int
gfarmfs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
	return (0);
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
	.getattr  = gfarmfs_getattr,
	.readlink = gfarmfs_readlink,
	.getdir   = gfarmfs_getdir,
	.mknod    = gfarmfs_mknod,
	.mkdir    = gfarmfs_mkdir,
	.symlink  = gfarmfs_symlink,
	.unlink   = gfarmfs_unlink,
	.rmdir    = gfarmfs_rmdir,
	.rename   = gfarmfs_rename,
	.link     = gfarmfs_link,
	.chmod    = gfarmfs_chmod,
	.chown    = gfarmfs_chown,
	.truncate = gfarmfs_truncate,
	.utime    = gfarmfs_utime,
	.open     = gfarmfs_open,
	.read     = gfarmfs_read,
	.write    = gfarmfs_write,
	.release  = gfarmfs_release,
#if 0
	.fsync    = gfarmfs_fsync,
	.statfs   = gfarmfs_statfs,
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
	fprintf(stdout, "GfarmFS-FUSE version %s (%s)\n",
		GFARMFS_VER, GFARMFS_VER_DATE);
}

static void
gfarmfs_usage()
{
	const char *fusehelp[] = { program_name, "-ho", NULL };

	fprintf(stderr,
"Usage: %s [GfarmFS options] <mountpoint> [FUSE options]\n"
"\n"
"GfarmFS options:\n"
#ifdef SYMLINK_MODE
"    -s, --symlink          enable symlink(2) to work (emulation)\n"
#endif
"    -l, --linkiscopy       enable link(2) to behave copying a file (emulation)\n"
"    -a <architecture>      for a client not registered by gfhost\n"
"    -u, --unlinkall        enable unlink(2) to remove all architecture files\n"
"    -f, --fastcreate       improve performance of file creation\n"
"                           (MKNOD does not flush the meta data)\n"
"    -v, --version          show version and exit\n"
"\n", program_name);

	fuse_main(2, (char **) fusehelp, &gfarmfs_oper);
}

static void
check_fuse_options(int *argcp, char ***argvp)
{
	int argc = *argcp;
	char **argv = *argvp;
	char **newargv;
	int i;
	int ok_s = 0; /* check -s */
	char *opt_s_str = "-s";

	for(i = 1; i < argc; i++) {
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
		for (i = 0; i < argc; i++) {
			newargv[i] = argv[i];
		}
		argc++;
		newargv[argc - 1] = opt_s_str;
		*argcp = argc;
		*argvp = newargv;
	}
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
		if (strcmp(&argv[0][1], "-symlink") == 0 ||
		    strcmp(&argv[0][1], "s") == 0) {
			enable_symlink = 1;
		} else if (strcmp(&argv[0][1], "-linkiscopy") == 0 ||
			   strcmp(&argv[0][1], "l") == 0) {
			enable_linkiscopy = 1;
		} else if (strcmp(&argv[0][1], "-architecture") == 0 ||
			   strcmp(&argv[0][1], "a") == 0) {
			--argc;
			++argv;
			if (argc > 0) {
				arch_name = *argv;
			} else {
				gfarmfs_usage();
				exit(1);
			}
		} else if (strcmp(&argv[0][1], "-unlinkall") == 0 ||
			   strcmp(&argv[0][1], "u") == 0) {
			enable_unlinkall = 1;
		} else if (strcmp(&argv[0][1], "-fastcreate") == 0 ||
			   strcmp(&argv[0][1], "f") == 0) {
			enable_fastcreate = 1;
		} else if (strcmp(&argv[0][1], "-unbuf") == 0) {
			enable_gfarm_unbuf = 1;
		} else if (strcmp(&argv[0][1], "-version") == 0 ||
			   strcmp(&argv[0][1], "v") == 0) {
			gfarmfs_version();
			exit(0);
		} else {
			gfarmfs_usage();
			exit(1);
		}
		--argc;
		++argv;
	}
	++argc;
	--argv;
	*argcp = argc;
	*argv = argv0;
	*argvp = argv;
}

int
main(int argc, char *argv[])
{
	int ret;
	char *e;

	if (argc > 0) {
		program_name = basename(argv[0]);
	}
	/* e = gfarm_initialize(&argc, &argv); */
	e = gfarm_initialize(NULL, NULL);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(-1);
	}
	check_gfarmfs_options(&argc, &argv);
	check_fuse_options(&argc, &argv);

	ret = fuse_main(argc, argv, &gfarmfs_oper);
	gfarmfs_fastcreate_check();
	return (ret);
}
