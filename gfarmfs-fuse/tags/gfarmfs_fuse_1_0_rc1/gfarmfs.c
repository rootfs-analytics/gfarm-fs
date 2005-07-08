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

/* #define GFARMFS_EVERYINIT  (for test) */

/* ################################################################### */

int gfarmfs_debug = 0;  /* 1: error, 2: debug */
int enable_symlink = 0;
int enable_linkiscopy = 0;
int enable_gfarm_unbuf = 0;
char *archi_name = NULL;

/* This is necessary to free the memory space by free(). */
static char *
add_gfarm_prefix(const char *path)
{
    char *url;
    url = malloc(strlen(path) + 7);
    sprintf(url, "gfarm:%s", path);
    return url;
}

#ifdef SYMLINK_MODE
/* This is necessary to free the memory space by free(). */
static char *
add_gfarm_prefix_symlink_suffix(const char *path)
{
    char *url;
    url = malloc(strlen(path) + 7 + strlen(SYMLINK_SUFFIX));
    sprintf(url, "gfarm:%s%s", path, SYMLINK_SUFFIX);
    return url;
}
#endif

static char *
gfarmfs_init()
{
#ifdef GFARMFS_EVERYINIT
    return gfarm_initialize(NULL, NULL);
#else
    return NULL;
#endif
}

static int
gfarmfs_final(char *e, int val_noerror, const char *name)
{
#ifdef GFARMFS_EVERYINIT
    gfarm_terminate();
#endif

    if (e == NULL) {
        return val_noerror;
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
        while ((e = gfs_readdir(dir, &entry)) == NULL && entry != NULL) {
            if (S_ISDIR(DTTOIF(entry->d_type))) {
                res++;
            } 
        }
        gfs_closedir(dir);
    }
    if (res == 0) {
        return 2;
    } else {
        return res;
    }
}

#ifdef SYMLINK_MODE
int
ends_with_and_delete(char *str, char *suffix)
{
    int m, n;
    m = strlen(str) - 1;
    n = strlen(suffix) - 1;
    while (n >=0) {
        if (m <= 0 || str[m] != suffix[n]) { 
            return 0; /* false */
        }
        m--;
        n--;
    }
    str[m + 1] = '\0';
    return 1; /* true */
}
#endif

char *
gfarmfs_check_program_and_set_archi_using_url(GFS_File gf, char *url)
{
    char *e, *ex;

    if (archi_name != NULL) {
        ex = gfs_access(url, X_OK);
        if (ex == NULL) {
            e = gfs_pio_set_view_section(gf, archi_name, NULL, 0);
            return e;
        }
    }
    return NULL;
}

char *
gfarmfs_check_program_and_set_archi_using_mode(GFS_File gf, mode_t mode)
{
    char *e;

    if (archi_name != NULL && (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0) {
        e = gfs_pio_set_view_section(gf, archi_name, NULL, 0);
        return e;
    } else {
        return NULL;
    }
}

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

        if (gs.st_user != NULL && ((p = getpwnam(gs.st_user)) != NULL)) {
            buf->st_uid = p->pw_uid;
            buf->st_gid = p->pw_gid;
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
    int res;
    char *url;

    e = gfarmfs_init();
    if (e == NULL) {
        url = add_gfarm_prefix(path);
        e = gfs_opendir(url, &dir);
        free(url);
    }
    if (e == NULL) {
        while ((e = gfs_readdir(dir, &entry)) == NULL && entry != NULL) {
#ifdef SYMLINK_MODE
            if (enable_symlink == 1 &&
                ends_with_and_delete(entry->d_name, SYMLINK_SUFFIX) > 0) {
                entry->d_type = DT_LNK;
            }
#endif
            res = filler(h, entry->d_name, entry->d_type, entry->d_ino);    
            if (res != 0)
                break;
        }
        e = gfs_closedir(dir);
    }
    return gfarmfs_final(e, 0, path);
}

static int
gfarmfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
    char *e, *ex;
    GFS_File gf;
    char *url;

    if (rdev != 0 && ((rdev & S_IFREG) != S_IFREG)) {
        if (gfarmfs_debug >= 1) {
            printf("mknod: not supported: rdev = %d\n", (int)rdev);
        }
        return -ENOSYS;  /* XXX */
    }

    url = add_gfarm_prefix(path);
    e = gfarmfs_init();
    while (e == NULL) {
        e = gfs_pio_create(url, GFARM_FILE_WRONLY|GFARM_FILE_EXCLUSIVE,
                           mode, &gf);
        if (e != NULL) break;

        ex = gfarmfs_check_program_and_set_archi_using_mode(gf, mode);
        e = gfs_pio_close(gf);
        if (ex != NULL) e = ex;
        break;
    }
    free(url);

    return gfarmfs_final(e, 0, path);
}

static int
gfarmfs_mkdir(const char *path, mode_t mode)
{
    char *e;
    char *url;
  
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

    e = gfarmfs_init();
    if (e == NULL) {
        url = add_gfarm_prefix(path);
        e = gfs_unlink(url);
        free(url);
#ifdef SYMLINK_MODE
        if (enable_symlink == 1 && e != NULL) {
            if (gfarm_error_to_errno(e) == ENOENT) {
                url = add_gfarm_prefix_symlink_suffix(path);
                if (gfarmfs_debug >= 2) {
                    printf("unlink: for symlink: %s\n", path);
                }
                e = gfs_unlink(url);
                free(url);
            }
        }
#endif
    }

    return gfarmfs_final(e, 0, path);
}

static int
gfarmfs_rmdir(const char *path)
{
    char *e;
    char *url;
  
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

    if (enable_symlink == 0) {
        return -ENOSYS;
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
    return -ENOSYS;
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

    if (enable_symlink == 0) {
        return -ENOSYS;
    }
    url = add_gfarm_prefix_symlink_suffix(to);
    e = gfarmfs_init();
    while (e == NULL) {
        e = gfs_pio_create(url, GFARM_FILE_WRONLY|GFARM_FILE_EXCLUSIVE,
                           0644, &gf);
        if (e != NULL) break;
        len = strlen(from);
        e = gfs_pio_write(gf, from, len, &n);
        gfs_pio_close(gf);
        if (len != n) {
            e = gfs_unlink(url);
            gfarmfs_final(e, 0, to);
            free(url);
            return -EIO;
        }
        break;
    }
    free(url);

    return gfarmfs_final(e, 0, to);
#else
    return -ENOSYS;
#endif
}

static int
gfarmfs_rename(const char *from, const char *to)
{
    char *e;
    char *from_url;
    char *to_url;

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
                from_url = add_gfarm_prefix_symlink_suffix(from);
                to_url = add_gfarm_prefix_symlink_suffix(to);
                if (gfarmfs_debug >= 2) {
                    printf("rename: for symlink: %s\n", from);
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
    char *fromurl, *tourl;
    GFS_File fromgf, togf;
    int fromopened, toopened;
    struct gfs_stat gs;
    int mode;
    int m, n;
    char buf[4096];
    int symlinkmode = 0;

    if (enable_linkiscopy == 0) {
        return -ENOSYS;
    }

    if (gfarmfs_debug >= 2) {
        printf("hard link is replaced by copy: %s\n", to);
    }
    fromopened = toopened = 0;
    fromurl = add_gfarm_prefix(from);
    e = gfarmfs_init();
    while (e == NULL) {
        e = gfs_stat(fromurl, &gs);
#ifdef SYMLINK_MODE
        if (enable_symlink == 1 && e != NULL) {
            if (gfarm_error_to_errno(e) == ENOENT) {
                free(fromurl);
                fromurl = add_gfarm_prefix_symlink_suffix(from);
                if (gfarmfs_debug >= 2) {
                    printf("link: for symlink: %s\n", from);
                }
                e = gfs_stat(fromurl, &gs);
                symlinkmode = 1;
            }
        }
#endif
        if (e != NULL) break;
        mode = gs.st_mode;
        gfs_stat_free(&gs);

        e = gfs_pio_open(fromurl, GFARM_FILE_RDONLY, &fromgf);
        if (e != NULL) break;
        fromopened = 1;

        e = gfarmfs_check_program_and_set_archi_using_url(fromgf, fromurl);
        if (e != NULL) break;

        if (symlinkmode == 1) {
            tourl = add_gfarm_prefix_symlink_suffix(to);
        } else {
            tourl = add_gfarm_prefix(to);
        }
        e = gfs_pio_create(tourl, GFARM_FILE_WRONLY|GFARM_FILE_EXCLUSIVE,
                           mode, &togf);
        free(tourl);
        if (e != NULL) break;
        toopened = 1;

        e = gfarmfs_check_program_and_set_archi_using_mode(togf, mode);
        if (e != NULL) break;

        while(1) {
            e = gfs_pio_read(fromgf, buf, 4096, &m);
            if (e != NULL) break;
            if (m == 0) { /* EOF */
                break;
            }
            e = gfs_pio_write(togf, buf, m, &n);
            if (e != NULL) break;
            if (m != n) {
                e = GFARM_ERR_INPUT_OUTPUT;
                break;
            }
        }
        break;
    }
    free(fromurl);

    if (fromopened == 1) {
        gfs_pio_close(fromgf);
    }
    if (toopened == 1) {
        gfs_pio_close(togf);
    }
    return gfarmfs_final(e, 0, to);
}

static int
gfarmfs_chmod(const char *path, mode_t mode)
{
    char *e;
    char *url;
  
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
    
    e = gfarmfs_init();
    if (e == NULL) {
        url = add_gfarm_prefix(path);
        e = gfs_stat(url, &s);
        free(url);
        if (e == NULL) {
            if (strcmp(s.st_user, gfarm_get_global_username()) != 0) {
                e = GFARM_ERR_OPERATION_NOT_PERMITTED; /* EPERM */
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
  
    url = add_gfarm_prefix(path);
    e = gfarmfs_init();
    while (e == NULL) {
        e = gfs_pio_open(url, GFARM_FILE_WRONLY, &gf);
        if (e != NULL) break;

        e = gfarmfs_check_program_and_set_archi_using_url(gf, url);
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

    url = add_gfarm_prefix(path);
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
        e = gfs_pio_open(url, flags, &gf);
        if (e != NULL) break;
        
        e = gfarmfs_check_program_and_set_archi_using_url(gf, url);
        if (e != NULL) {
            gfs_pio_close(gf);
            break;
        }

        fi->fh = (unsigned long) gf;
        break;
    }
    free(url);

    return gfarmfs_final(e, 0, path);
}

static int
gfarmfs_release(const char *path, struct fuse_file_info *fi)
{
    char *e;
    GFS_File gf;

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
static int gfarmfs_statfs(const char *path, struct statfs *stbuf)
{
    return -ENOSYS;
}

static int
gfarmfs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    return 0;
}
#endif

#if 0
/* #ifdef HAVE_SETXATTR */
/* xattr operations are optional and can safely be left unimplemented */
static int
gfarmfs_setxattr(const char *path, const char *name, const char *value,
                 size_t size, int flags)
{
    return -ENOSYS;
}

static int
gfarmfs_getxattr(const char *path, const char *name, char *value,
                 size_t size)
{
    return -ENOSYS;
}

static int
gfarmfs_listxattr(const char *path, char *list, size_t size)
{
    return -ENOSYS;
}

static int
gfarmfs_removexattr(const char *path, const char *name)
{
    return -ENOSYS;
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations gfarmfs_oper = {
    .getattr	= gfarmfs_getattr,
    .readlink	= gfarmfs_readlink,
    .getdir	= gfarmfs_getdir,
    .mknod	= gfarmfs_mknod,
    .mkdir	= gfarmfs_mkdir,
    .symlink	= gfarmfs_symlink,
    .unlink	= gfarmfs_unlink,
    .rmdir	= gfarmfs_rmdir,
    .rename	= gfarmfs_rename,
    .link	= gfarmfs_link,
    .chmod	= gfarmfs_chmod,
    .chown	= gfarmfs_chown,
    .truncate	= gfarmfs_truncate,
    .utime	= gfarmfs_utime,
    .open	= gfarmfs_open,
    .read	= gfarmfs_read,
    .write	= gfarmfs_write,
    .release	= gfarmfs_release,
#if 0
    .fsync	= gfarmfs_fsync,
    .statfs	= gfarmfs_statfs,
#endif
#if 0
/* #ifdef HAVE_SETXATTR */
    .setxattr	= gfarmfs_setxattr,
    .getxattr	= gfarmfs_getxattr,
    .listxattr	= gfarmfs_listxattr,
    .removexattr= gfarmfs_removexattr,
#endif
};

/* ################################################################### */

char *program_name = "gfarmfs";

void
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
"\n", program_name);

    fuse_main(2, (char **) fusehelp, &gfarmfs_oper);
    exit(1);
}

void
check_fuse_options(int *argcp, char ***argvp)
{
    int argc = *argcp;
    char **argv = *argvp;
    char **newargv;
    int i;
    int ok_s = 0; /* check -s */
    static char *opt_s_str = "-s";

    for(i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) {
            ok_s = 1;
        } else if (strcmp(argv[i], "-f") == 0) {
            gfarmfs_debug = 1;
        } else if (strcmp(argv[i], "-d") == 0) {
            gfarmfs_debug = 2;
        } else if (strcmp(argv[i], "-h") == 0) {
            gfarmfs_usage();
            /* exit */
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

void
check_gfarmfs_options(int *argcp, char ***argvp)
{
    int argc = *argcp;
    char **argv = *argvp;
    char *argv0 = *argv;
    
    --argc;
    ++argv;
    while (argc > 0 && argv[0][0] == '-') {
        if (strcmp(&argv[0][1], "-symlink") == 0
            || strcmp(&argv[0][1], "s") == 0) {
            printf("enable symlink\n");
            enable_symlink = 1;
        } else if (strcmp(&argv[0][1], "-linkiscopy") == 0
                   || strcmp(&argv[0][1], "l") == 0) {
            printf("enable linkiscopy\n");
            enable_linkiscopy = 1;
        } else if (strcmp(&argv[0][1], "-architecture") == 0
                   || strcmp(&argv[0][1], "a") == 0) {
            --argc;
            ++argv;
            if (argc > 0) {
                archi_name = *argv;
                printf("set architecture: `%s'\n", archi_name);
            } else {
                gfarmfs_usage();
                /* exit */
            }           
        } else if (strcmp(&argv[0][1], "-unbuf") == 0) {
            printf("enable GFARM_FILE_UNBUFFERED\n");
            enable_gfarm_unbuf = 1;
        } else {
            gfarmfs_usage();
            /* exit */
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
#ifndef GFARMFS_EVERYINIT
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
#endif

    check_gfarmfs_options(&argc, &argv);
    check_fuse_options(&argc, &argv);

    return fuse_main(argc, argv, &gfarmfs_oper);
}
