/*
 *  Test program for syscall hooking feature.
 *
 *  $Id$
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/syscall.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>

#include <stdio.h>

#include <gfarm/gfarm.h>
#include <gfarm/gfs_hook.h>

/*
 *  Display struct stat.
 *
 *  Known bug: Device is not correctly displayed.
 */

#ifndef HAVE_GTIME
#define gtime(x) asctime(gmtime(x))
#endif

#ifndef S_IAMB
#define S_IAMB 07777
#endif

void
display_stat(char *fn, struct stat *st)
{
    printf("  File: \"%s\"\n", fn);
    printf("  Size: %-12ld Filetype: ", (long)st->st_size);
    switch (st->st_mode & S_IFMT) {
    case S_IFSOCK:
	puts("socket");
	break;
    case S_IFLNK:
	puts("symbolic link");
	break;
    case S_IFREG:
	puts("regular file");
	break;
    case S_IFBLK:
	puts("block device");
	break;
    case S_IFDIR:
	puts("directory");
	break;
    case S_IFCHR:
	puts("character device");
	break;
    case S_IFIFO:
	puts("fifo");
	break;
    default:
	printf("unknown\n");
    }
#if 0
    printf("  Mode: (%04o) Uid: (%5d/%8s) Gid: (%5d/%8s)\n",
	   st->st_mode & S_IAMB, st->st_uid,
	   getpwuid(st->st_uid)->pw_name,
	   st->st_gid, getgrgid(st->st_gid)->gr_name);
    printf("Device: %d,%d Inode: %ld Links: %d\n",
	   st->st_dev, st->st_rdev, st->st_ino, st->st_nlink);
#endif
    printf("Access: %s", gtime(&st->st_atime));
    printf("Modify: %s", gtime(&st->st_mtime));
    printf("Change: %s", gtime(&st->st_ctime));
}

/*
 * test functions
 */

int test_stat(char *filename)
{
    struct stat s;
    int r;

    printf("***** stat(%s)\n", filename);
    if ((r = stat(filename, &s)))
	perror(filename);
    else
	display_stat(filename, &s);

    return r;
}


int test_lstat(char *filename)
{
    struct stat s;
    int r;

    printf("***** lstat(%s)\n", filename);
    if ((r = lstat(filename, &s)))
	perror(filename);
    else
	display_stat(filename, &s);

    return r;
}

void test_fstat(int filedes)
{
    struct stat s;

    printf("***** fstat(%d)\n", filedes);
    if (fstat(filedes, &s))
	perror("fstat");
    else
	display_stat("test_fstat", &s);
}

int test_access(char *filename)
{
    int r;

    printf("***** access(%s, %d)\n", filename, R_OK);
    if ((r = access(filename, R_OK)))
	perror(filename);

    return r;
}

int test_open_create(char *filename)
{
    int fd;

    printf("***** open(%s, O_WRONLY|O_CREAT|O_TRUNC, 0664)\n", filename);
    if ((fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0664)) == -1)
	perror(filename), exit(1);

    return fd;
}

int test_open_rdonly(char *filename)
{
    int fd;

    printf("***** open(%s, O_RDONLY)\n", filename);
    if ((fd = open(filename, O_RDONLY)) == -1)
	perror(filename), exit(1);

    return fd;
}

void test_write_string(int fd, char *teststring)
{
    printf("***** write(%d, %d)\n", fd, strlen(teststring) + 1);
    if (write(fd, teststring, strlen(teststring) + 1) == -1)
	perror("write"), exit(1);
}

void test_read_string(int fd)
{
#   define BUFSIZE 100
    char buf[BUFSIZE];
    int n;

    printf("***** read(%d, %d)\n", fd, sizeof(buf));
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
	int i;
	for (i = 0; i < n; ++i)
	    if (buf[i])
	    	printf("%c", buf[i]);
    }
}

void test_lseek_set(int fd, off_t offset)
{
    printf("***** lseek(%d, %d, SEEK_SET)\n", fd, (int)offset);
    if (lseek(fd, offset, SEEK_SET) == -1)
	perror("lseek"), exit(1);
}

void test_close(int fd)
{
    printf("***** close(%d)\n", fd);
    if (close(fd))
	perror("close"), exit(1);
}

void test_unlink(char *filename)
{
    printf("***** unlink(%s)\n", filename);
    if (unlink(filename))
	perror(filename), exit(1);
}

/*
 * stdio
 */

FILE *test_fopen(char *filename, char *mode)
{
    FILE *f;

    printf("***** fopen(%s, \"%s\")\n", filename, mode);
    if ((f = fopen(filename, mode)) == NULL)
	perror(filename), exit(1);

    return f;
}

void test_fgetc(FILE *f)
{
    int c;

    printf("***** fgetc(%d)\n", fileno(f));
    while ((c = fgetc(f)) != EOF)
	printf("%c", c);
}

void test_fseek_set(FILE *f, long offset)
{
    printf("***** fseek(%d, %ld, SEEK_SET)\n", fileno(f), offset);
    if (fseek(f, offset, SEEK_SET))
	perror("fseek"), exit(1);
}

void test_ftell(FILE *f)
{
    long pos;

    printf("***** ftell(%d)\n", fileno(f));
    pos = ftell(f);
    if (pos == -1)
	perror("ftell"), exit(1);
    printf("pos = %ld\n", pos);
}

void test_rewind(FILE *f)
{
    printf("***** rewind(%d)\n", fileno(f));
    rewind(f);
}

void test_fseeko_set(FILE *f, off_t offset)
{
    printf("***** fseeko(%d, %ld, SEEK_SET)\n", fileno(f), (long)offset);
    if (fseeko(f, offset, SEEK_SET))
	perror("fseeko"), exit(1);
}

void test_ftello(FILE *f)
{
    off_t pos;

    printf("***** ftello(%d)\n", fileno(f));
    pos = ftello(f);
    if (pos == -1)
	perror("ftello"), exit(1);
    printf("pos = %ld\n", (long)pos);
}

void test_fclose(FILE *f)
{
    printf("***** fclose(%d)\n", fileno(f));
    if (fclose(f))
	perror("fclose"), exit(1);
}

void test_dup2(int oldfd, int newfd)
{
    printf("***** dup2(%d, %d)\n", oldfd, newfd);
    if (dup2(oldfd, newfd) == -1)
	perror("dup2"), exit(1);
}

/*
 *
 */

int
main(int argc, char *argv[])
{
    char *filename = "gfarm:gfs_hook_test", *e;
    static char teststring[] = "Gfarm hook test!\n";
    int fd;
    FILE *f;

    setvbuf(stdout, NULL, _IONBF, 0); /* to sync with truss/strace output */

    /*
     * If gfarm_initialize() is not called, this program is assumed
     * to be a serial program, i.e. gfs_pio_set_local(0, 1) is
     * automatically called.

    e = gfarm_initialize(&argc, &argv);
    if (e != NULL)
	fprintf(stderr, "%s: %s\n", argv[0], e), exit(1);
    */

    if (argc > 1)
	filename = argv[1];

#if 0
    /* create a file */

    if (test_stat(filename) == 0)
	test_unlink(filename);

    fd = test_open_create(filename);
    test_write_string(fd, teststring);
    test_lseek_set(fd, 0);
    test_fstat(fd);
    test_close(fd);

    test_access(filename);

    test_stat(filename);
    test_lstat(filename);

    /* read the file */

    fd = test_open_rdonly(filename);
    test_read_string(fd);
    test_lseek_set(fd, 0);
    test_read_string(fd);
    test_fstat(fd);
    test_close(fd);

    test_stat(filename);
    test_lstat(filename);

    /* unlink the file */

    test_unlink(filename);

    f = test_fopen(filename, "w");
    fprintf(f, "%s", teststring);
    test_fclose(f);

    f = test_fopen(filename, "r");
    test_fgetc(f);
    test_fseek_set(f, 0);
    test_ftell(f);
    test_rewind(f);
    test_fseeko_set(f, 0);
    test_ftello(f);
    test_fclose(f);

    test_unlink(filename);

    /* read the dirctory entries */
    int i;

    for (i = 0; i < 1000000; i++) {
	fd = test_open_rdonly(filename);
    	test_close(fd);
    }
#endif
    
#include <dirent.h>

#define STRUCT_DIRENT	struct dirent
#define FUNC_GETDENTS	getdents

#if 0
    char buff[1024];
    STRUCT_DIRENT *p;
    int rcount;
    
    fd = test_open_rdonly(filename);
    while((rcount=FUNC_GETDENTS(fd, (STRUCT_DIRENT *)buff, 256)) > 0) {

	xdump( buff, rcount );

	printf("rcount:%d\n", rcount);
            for ( p = (STRUCT_DIRENT *)buff ; (char *)p < &buff[rcount] ;
		 p = (STRUCT_DIRENT *)((char *)p + p->d_reclen)) {

                printf("p:%d, ", (char *)p - buff );
                printf("off:%u, ", p->d_off );  /* long */
                printf("ino:%u, ", p->d_ino );  /* unsigned long */
                printf("reclen:%d, ", p->d_reclen );
                printf("name:%s\n", p->d_name );
            }
    }

    test_close(fd);
#else
	DIR *dirp;
	struct dirent *dp;

	if (argc < 2)
		dirp = opendir(".");
	else
		dirp = opendir(argv[1]);
	if (dirp != NULL) {
		while ((dp = readdir(dirp)) != NULL)
			printf("%s\n", dp->d_name);
		(void)closedir(dirp);
	}
#endif

#if 0

    /* "Is a directory" check */

    fd = test_open_rdonly(argv[1]);
    /* test_lseek_set(fd, 0); */
    /* test_dup2(fd, fd + 1); */
    test_close(fd);
    /* test_close(fd + 1); */

    /* fd = dup2(fd, 7); */
    /* printf("%d\n", fd); */

    /* test_read_string(fd); */
    /* test_write_string(fd, teststring); */
    /* test_unlink(argv[1]); */
    /* test_access(argv[1]); */
    /* fd = creat(argv[1], 0664); */

    /* static char *execve_argv[] = { NULL, NULL }; */
    /* static char *execve_envp[] = { NULL }; */
    /* execve_argv[0] = argv[1]; */
    /* execve(argv[1], execve_argv, execve_envp); */
    /* utimes(argv[1], NULL); */

    /* gfarm_terminate(); */
#endif
    perror("");
    exit(0);
}
