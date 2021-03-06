/*
 * $Id$
 */

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <netdb.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <syslog.h>
#include <stdarg.h>
#include <setjmp.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <pwd.h>
#include <grp.h>
#include <libgen.h>

#include <openssl/evp.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#define GFLOG_USE_STDARG
#include "gfutil.h"
#include "iobuffer.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "param.h"
#include "sockopt.h"
#include "hostspec.h"
#include "host.h"
#include "auth.h"
#include "config.h"
#include "gfs_proto.h"
#include "gfs_client.h"
#include "gfm_proto.h"
#include "gfm_client.h"
#include "metadb_server.h"

#include "gfsd_subr.h"

#define COMPAT_OLD_GFS_PROTOCOL

#ifndef DEFAULT_PATH
#define DEFAULT_PATH "PATH=/usr/local/bin:/usr/bin:/bin:/usr/ucb:/usr/X11R6/bin:/usr/openwin/bin:/usr/pkg/bin"
#endif

#define GFARM_DEFAULT_PATH	DEFAULT_PATH ":" GFARM_DEFAULT_BINDIR

#ifndef PATH_BSHELL
#define PATH_BSHELL "/bin/sh"
#endif

#define LOCAL_SOCKDIR_MODE	0755
#define LOCAL_SOCKET_MODE	0777
#define PERMISSION_MASK		0777

/* need to be accessed as an executable (in future, e.g. after chmod) */
#define	DATA_FILE_MASK		0711
#define	DATA_DIR_MASK		0700

#ifdef SOMAXCONN
#define LISTEN_BACKLOG	SOMAXCONN
#else
#define LISTEN_BACKLOG	5
#endif

/* limit maximum open files per client, when system limit is very high */
#ifndef FILE_TABLE_LIMIT
#define FILE_TABLE_LIMIT	2048
#endif

char *program_name = "gfsd";

int debug_mode = 0;
pid_t master_gfsd_pid;

int restrict_user = 0;
uid_t restricted_user = 0;

uid_t gfsd_uid;
mode_t command_umask;

char local_sockdir[PATH_MAX];
struct sockaddr_un local_sockname;

struct gfm_connection *gfm_server;
char *username; /* gfarm global user name */

struct gfp_xdr *credential_exported = NULL;

#if 0 /* not yet in gfarm v2 */
long file_read_size;
long rate_limit;
#endif

/* this routine should be called before calling exit(). */
void
cleanup(int sighandler)
{
	if (getpid() == master_gfsd_pid) {
		unlink(local_sockname.sun_path);
		rmdir(local_sockdir);
	}

	if (credential_exported != NULL)
		gfp_xdr_delete_credential(credential_exported, sighandler);
	credential_exported = NULL;

	if (sighandler) /* It's not safe to do the following operation */
		return;

	/* disconnect, do logging */
	gflog_notice("disconnected");
}

void
parent_cleanup(void)
{
}

void
cleanup_handler(int signo)
{
	cleanup(1);
	_exit(2);
}

void vfatal(const char *, va_list) GFLOG_PRINTF_ARG(1, 0);
void
vfatal(const char *format, va_list ap)
{
	cleanup(0);

	gflog_vmessage(LOG_ERR, format, ap);
}

void fatal(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
void
fatal(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vfatal(format, ap);
	va_end(ap);
	exit(2);
}

void fatal_errno(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
void
fatal_errno(const char *format, ...)
{
	char buffer[2048];
	va_list ap;

	va_start(ap, format);
	vsnprintf(buffer, sizeof buffer, format, ap);
	va_end(ap);
	fatal("%s: %s", buffer, strerror(errno));
}

void
fatal_metadb_proto(const char *diag, const char *proto, gfarm_error_t e)
{
	fatal("gfmd protocol: %s error on %s: %s", proto, diag,
	      gfarm_error_string(e));
}

static int
fd_send_message(int fd, void *buffer, size_t size, int fdc, int *fdv)
{
	int i, rv;
	struct iovec iov[1];
	struct msghdr msg;
#ifdef SCM_RIGHTS /* 4.3BSD Reno or later */
	struct {
		struct cmsghdr hdr;
		char data[CMSG_SPACE(sizeof(*fdv) * GFSD_MAX_PASSING_FD)
			  - sizeof(struct cmsghdr)];
	} cmsg;

	if (fdc > GFSD_MAX_PASSING_FD) {
		fatal("gfsd: fd_send_message(): fd count %d > %d",
		    fdc, GFSD_MAX_PASSING_FD);
		return (EINVAL);
	}
#endif

	while (size > 0) {
		iov[0].iov_base = buffer;
		iov[0].iov_len = size;
		msg.msg_iov = iov;
		msg.msg_iovlen = 1;
		msg.msg_name = NULL;
		msg.msg_namelen = 0;
#ifndef SCM_RIGHTS
		if (fdc > 0) {
			msg.msg_accrights = (caddr_t)fdv;
			msg.msg_accrightslen = sizeof(*fdv) * fdc;
		} else {
			msg.msg_accrights = NULL;
			msg.msg_accrightslen = 0;
		}
#else /* 4.3BSD Reno or later */
		if (fdc > 0) {
			msg.msg_control = (caddr_t)&cmsg.hdr;
			msg.msg_controllen = CMSG_SPACE(sizeof(*fdv) * fdc);
			cmsg.hdr.cmsg_len = CMSG_LEN(sizeof(*fdv) * fdc);
			cmsg.hdr.cmsg_level = SOL_SOCKET;
			cmsg.hdr.cmsg_type = SCM_RIGHTS;
			for (i = 0; i < fdc; i++)
				((int *)CMSG_DATA(&cmsg.hdr))[i] = fdv[i];
		} else {
			msg.msg_control = NULL;
			msg.msg_controllen = 0;
		}
#endif
		rv = sendmsg(fd, &msg, 0);
		if (rv == -1) {
			if (errno == EINTR)
				continue;
			return (errno); /* failure */
		}
		fdc = 0; fdv = NULL;
		buffer += rv;
		size -= rv;
	}
	return (0); /* success */
}

void
gfs_server_get_request(struct gfp_xdr *client, const char *diag,
	const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	int eof;

	va_start(ap, format);
	e = gfp_xdr_vrecv(client, 0, &eof, &format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR)
		fatal("%s: %s", diag, gfarm_error_string(e));
	if (eof)
		fatal("%s: missing RPC argument", diag);
	if (*format != '\0')
		fatal("%s: invalid format character to get request", diag);
}

void
gfs_server_put_reply_common(struct gfp_xdr *client, const char *diag,
	gfarm_int32_t ecode, const char *format, va_list *app)
{
	gfarm_error_t e;

	e = gfp_xdr_send(client, "i", ecode);
	if (e != GFARM_ERR_NO_ERROR)
		fatal("%s: %s", diag, gfarm_error_string(e));
	if (ecode == GFARM_ERR_NO_ERROR) {
		e = gfp_xdr_vsend(client, &format, app);
		if (e != GFARM_ERR_NO_ERROR)
			fatal("%s: %s", diag, gfarm_error_string(e));
	}

	if (ecode == GFARM_ERR_NO_ERROR && *format != '\0')
		fatal("%s: invalid format character `%c' to put reply",
		    diag, *format);
}

void
gfs_server_put_reply(struct gfp_xdr *client, const char *diag,
	int ecode, char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	gfs_server_put_reply_common(client, diag, ecode, format, &ap);
	va_end(ap);
}

void
gfs_server_put_reply_with_errno(struct gfp_xdr *client, const char *diag,
	int eno, char *format, ...)
{
	va_list ap;
	gfarm_int32_t ecode = gfarm_errno_to_error(eno);

	if (ecode == GFARM_ERR_UNKNOWN)
		gflog_info("%s: %s", diag, strerror(eno));
	va_start(ap, format);
	gfs_server_put_reply_common(client, diag, ecode, format, &ap);
	va_end(ap);
}

void
gfs_server_process_set(struct gfp_xdr *client)
{
	gfarm_int32_t e;
	gfarm_pid_t pid;
	gfarm_int32_t keytype;
	size_t keylen;
	char sharedkey[GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET];

	gfs_server_get_request(client, "process_set",
	    "ibl", &keytype, sizeof(sharedkey), &keylen, sharedkey, &pid);

	e = gfm_client_process_set(gfm_server,
	    keytype, sharedkey, keylen, pid);

	gfs_server_put_reply(client, "process_set", e, "");
}

int file_table_size = 0;

struct file_entry {
	off_t size;
	time_t mtime, atime; /* XXX FIXME tv_nsec */
	int flags, local_fd;
#define FILE_FLAG_LOCAL		0x01
#define FILE_FLAG_CREATED	0x02
#define FILE_FLAG_WRITABLE	0x04
#define FILE_FLAG_WRITTEN	0x08
#define FILE_FLAG_READ		0x10
} *file_table;

void
file_table_init(int table_size)
{
	int i;

	file_table = malloc(sizeof(*file_table) * table_size);
	if (file_table == NULL) {
		errno = ENOMEM; fatal_errno("file table");
	}
	for (i = 0; i < table_size; i++)
		file_table[i].local_fd = -1;
	file_table_size = table_size;
}

int
file_table_is_available(gfarm_int32_t net_fd)
{
	if (0 <= net_fd && net_fd < file_table_size)
		return (file_table[net_fd].local_fd == -1);
	else
		return (0);
}

void
file_table_add(gfarm_int32_t net_fd, int local_fd, int flags)
{
	struct file_entry *fe;
	struct stat st;

	if (fstat(local_fd, &st) < 0)
		fatal_errno("file_table_add: fstat failed");
	fe = &file_table[net_fd];
	fe->local_fd = local_fd;
	fe->flags = 0;
	if (flags & O_CREAT)
		fe->flags |= FILE_FLAG_CREATED;
	if ((flags & O_ACCMODE) != O_RDONLY)
		fe->flags |= FILE_FLAG_WRITABLE;
	fe->atime = st.st_atime;
	/* XXX FIXME st_atimespec.tv_nsec */
	fe->mtime = st.st_mtime;
	/* XXX FIXME st_mtimespec.tv_nsec */
	fe->size = st.st_size;
}

gfarm_error_t
file_table_close(gfarm_int32_t net_fd)
{
	gfarm_error_t e;

	if (net_fd < 0 || net_fd >= file_table_size ||
	    file_table[net_fd].local_fd == -1)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	if (close(file_table[net_fd].local_fd) < 0)
		e = gfarm_errno_to_error(errno);
	else
		e = GFARM_ERR_NO_ERROR;
	file_table[net_fd].local_fd = -1;
	return (e);
}

int
file_table_get(gfarm_int32_t net_fd)
{
	if (0 <= net_fd && net_fd < file_table_size)
		return (file_table[net_fd].local_fd);
	else
		return (-1);
}

struct file_entry *
file_table_entry(gfarm_int32_t net_fd)
{
	if (0 <= net_fd && net_fd < file_table_size)
		return (&file_table[net_fd]);
	else
		return (NULL);
}

void
file_table_set_flag(gfarm_int32_t net_fd, int flags)
{
	struct file_entry *fe = file_table_entry(net_fd);

	if (fe != NULL)
		fe->flags |= flags;
}

void
file_table_set_read(gfarm_int32_t net_fd)
{
	struct file_entry *fe = file_table_entry(net_fd);
	struct timeval now;

	fe->flags |= FILE_FLAG_READ;

	gettimeofday(&now, NULL);
	fe->atime = now.tv_sec;
	/* XXX FIXME st_atimespec.tv_nsec */
}

void
file_table_set_written(gfarm_int32_t net_fd)
{
	struct file_entry *fe = file_table_entry(net_fd);
	struct timeval now;

	fe->flags |= FILE_FLAG_WRITTEN;

	gettimeofday(&now, NULL);
	fe->mtime = now.tv_sec;
	/* XXX FIXME st_mtimespec.tv_nsec */
}

int
gfs_open_flags_localize(int open_flags)
{
	int local_flags;

	switch (open_flags & GFARM_FILE_ACCMODE) {
	case GFARM_FILE_RDONLY:	local_flags = O_RDONLY; break;
	case GFARM_FILE_WRONLY:	local_flags = O_WRONLY; break;
	case GFARM_FILE_RDWR:	local_flags = O_RDWR; break;
	default: return (-1);
	}

#if 0
	if ((open_flags & GFARM_FILE_CREATE) != 0)
		local_flags |= O_CREAT;
#endif
	if ((open_flags & GFARM_FILE_TRUNC) != 0)
		local_flags |= O_TRUNC;
#if 0 /* not yet in gfarm v2 */
	if ((open_flags & GFARM_FILE_APPEND) != 0)
		local_flags |= O_APPEND;
	if ((open_flags & GFARM_FILE_EXCLUSIVE) != 0)
		local_flags |= O_EXCL;
#endif /* not yet in gfarm v2 */
	return (local_flags);
}

/*
 * if inum == 0x0011223344556677, and gen == 0X8899AABBCCDDEEFF, then
 * local_path = gfarm_spool_root + "data/00112233/44/55/66/778899AABBCCDDEEFF".
 *
 * If the metadata server uses inum > 0x700000000000,
 * We need a modern filesystem which satisfies follows:
 * - can create more than 32765 (= 32767 - 1 (for current) - 1 (for parent))
 *   subdirectories.
 *   32767 comes from platforms which st_nlink is 16bit signed integer.
 *   ext2/ext3fs can create only 32000 subdirectories at maximum.
 * - uses B-tree or similar mechanism to search directory entries
 *   to avoid overhead of linear search.
 */

void
local_path(gfarm_ino_t inum, gfarm_uint64_t gen, char *diag, char **pathp)
{
	char *p;
	static int length = 0;
	static char template[] = "/data/00112233/44/55/66/778899AABBCCDDEEFF";
#define DIRLEVEL 5 /* there are 5 levels of directories in template[] */

	if (length == 0)
		length = strlen(gfarm_spool_root) + sizeof(template);
	
	p = malloc(length);
	if (p == NULL) {
		fatal("%s: no memory for %d bytes", diag, length);
	}
	snprintf(p, length, "%s/data/%08X/%02X/%02X/%02X/%02X%08X%08X",
	    gfarm_spool_root,
	    (unsigned int)((inum >> 32) & 0xffffffff),
	    (unsigned int)((inum >> 24) & 0xff),
	    (unsigned int)((inum >> 16) & 0xff),
	    (unsigned int)((inum >>  8) & 0xff),
	    (unsigned int)( inum        & 0xff),
	    (unsigned int)((gen  >> 32) & 0xffffffff),
	    (unsigned int)( gen         & 0xffffffff));
	*pathp = p;
}

int
open_data(char *path, int flags)
{
	int i, j, tail, slashpos[DIRLEVEL];
	int fd = open(path, flags, DATA_FILE_MASK);
	struct stat st;

	if (fd >= 0)
		return (fd);
	if ((flags & O_CREAT) == 0 || errno != ENOENT)
		return (-1); /* with errno */

	/* errno == ENOENT, so, maybe we don't have an ancestor directory */
	tail = strlen(path);
	for (i = 0; i < DIRLEVEL; i++) {
		for (--tail; tail > 0 && path[tail] != '/'; --tail)
			;
		if (tail <= 0) {
			gflog_warning("something wrong in local_path(): %s\n",
			    path);
			errno = ENOENT;
			return (-1);
		}
		assert(path[tail] == '/');
		slashpos[i] = tail;
		path[tail] = '\0';

		if (stat(path, &st) < 0) {
			/* maybe race? */
		} else if (errno != ENOENT) {
			gflog_warning("stat(`%s') failed: %s",
			    path, strerror(errno));
			errno = ENOENT;
			return (-1);
		} else if (mkdir(path, DATA_DIR_MASK) < 0) {
			if (errno == ENOENT)
				continue;
			if (errno == EEXIST) {
				/* maybe race */
			} else {
				gflog_warning("mkdir(`%s') failed: %s",
				    path, strerror(errno));
				errno = ENOENT;
				return (-1);
			}
		}
		/* Now, we have the ancestor directory */
		for (j = i;; --j) {
			path[slashpos[j]] = '/';
			if (j <= 0)
				break;
			if (mkdir(path, DATA_DIR_MASK) < 0) {
				if (errno == EEXIST) /* maybe race */
					continue;
				gflog_warning(
				    "unexpected mkdir(`%s') failure: %s",
				    path, strerror(errno));
				errno = ENOENT;
				return (-1);
			}
		}
		return (open(path, flags, DATA_FILE_MASK)); /* with errno */
	}
	gflog_warning("gfsd spool_root doesn't exist?: %s\n", path);
	errno = ENOENT;
	return (-1);
}

gfarm_error_t
gfs_server_open_common(struct gfp_xdr *client, char *diag,
	gfarm_int32_t *net_fdp, int *local_fdp)
{
	gfarm_error_t e;
	gfarm_int32_t net_fd;
	gfarm_ino_t ino;
	gfarm_uint64_t gen;
	gfarm_int32_t mode, net_flags, to_create;
	char *path;
	int local_fd, local_flags;

	gfs_server_get_request(client, diag, "i", &net_fd);

	if (!file_table_is_available(net_fd))
		e = GFARM_ERR_BAD_FILE_DESCRIPTOR;

	else if ((e = gfm_client_compound_begin_request(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto("compound_begin request", diag, e);
	else if ((e = gfm_client_put_fd_request(gfm_server, net_fd))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto("put_fd request", diag, e);
	else if ((e = gfm_client_reopen_request(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto("reopen request", diag, e);
	else if ((e = gfm_client_compound_end_request(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto("compound_end request", diag, e);

	else if ((e = gfm_client_compound_begin_result(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto("compound_begin result", diag, e);
	else if ((e = gfm_client_put_fd_result(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto("put_fd result", diag, e);
	else if ((e = gfm_client_reopen_result(gfm_server,
	    &ino, &gen, &mode, &net_flags, &to_create))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto("reopen result", diag, e);
	else if ((e = gfm_client_compound_end_result(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		fatal_metadb_proto("compound_end result", diag, e);

	else if (!GFARM_S_ISREG(mode))
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((local_flags = gfs_open_flags_localize(net_flags)) == -1)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else {
		local_path(ino, gen, diag, &path);
		if (to_create)
			local_flags |= O_CREAT;
		if ((local_fd = open_data(path, local_flags)) < 0) {
			e = gfarm_errno_to_error(errno);
		} else {
			file_table_add(net_fd, local_fd, local_flags);
			*net_fdp = net_fd;
			*local_fdp = local_fd;
		}
		free(path);
	}

	gfs_server_put_reply(client, diag, e, "");
	return (e);
}

void
gfs_server_open(struct gfp_xdr *client)
{
	gfarm_int32_t net_fd;
	int local_fd;

	gfs_server_open_common(client, "open", &net_fd, &local_fd);
}

void
gfs_server_open_local(struct gfp_xdr *client)
{
	gfarm_int32_t net_fd;
	int local_fd;
	gfarm_int8_t dummy = 0; /* needs at least 1 byte */

	if (gfs_server_open_common(client, "open_local", &net_fd, &local_fd) !=
	    GFARM_ERR_NO_ERROR)
		return;

	gfp_xdr_flush(client);
	/* XXX: FIXME layering violation */
	fd_send_message(gfp_xdr_fd(client),
	    &dummy, sizeof(dummy), 1, &local_fd);

	file_table_set_flag(net_fd, FILE_FLAG_LOCAL);
}

gfarm_error_t
close_request(struct file_entry *fe)
{
	if (fe->flags & FILE_FLAG_WRITTEN) {
		return (gfm_client_close_write_request(gfm_server,
		    (gfarm_int64_t)fe->atime, (gfarm_int32_t)0,
		    (gfarm_int64_t)fe->mtime, (gfarm_int32_t)0));
		/* XXX FIXME st_atimespec.tv_nsec */
		/* XXX FIXME st_mtimespec.tv_nsec */
	} else if (fe->flags & FILE_FLAG_READ) {
		return (gfm_client_close_read_request(gfm_server,
		    (gfarm_int64_t)fe->atime, (gfarm_int32_t)0));
		/* XXX FIXME st_atimespec.tv_nsec */
	} else {
		return (gfm_client_close_request(gfm_server));
	}
}

gfarm_error_t
close_result(struct file_entry *fe)
{
	if (fe->flags & FILE_FLAG_WRITTEN) {
		return (gfm_client_close_write_result(gfm_server));
	} else if (fe->flags & FILE_FLAG_READ) {
		return (gfm_client_close_read_result(gfm_server));
	} else {
		return (gfm_client_close_result(gfm_server));
	}
}

void
gfs_server_close(struct gfp_xdr *client)
{
	gfarm_error_t e, e2;
	int fd;
	struct file_entry *fe;
	struct stat st;
	static const char diag[] = "close";

	gfs_server_get_request(client, diag, "i", &fd);

	if ((fe = file_table_entry(fd)) == NULL)
		e = GFARM_ERR_BAD_FILE_DESCRIPTOR;
	else {
		if ((fe->flags & FILE_FLAG_LOCAL) == 0) { /* remote? */
			;
		} else if (fstat(fe->local_fd, &st) == -1) {
			gflog_warning("fd %d: stat failed at close: %s",
			    fd, strerror(errno));
		} else {
			if (st.st_atime != fe->atime) {
				fe->atime = st.st_atime;
				fe->flags |= FILE_FLAG_READ;
			}
			/* XXX FIXME st_atimespec.tv_nsec */

			if (st.st_mtime != fe->mtime) {
				fe->mtime = st.st_mtime;
				fe->flags |= FILE_FLAG_WRITTEN;
			}
			/* XXX FIXME st_mtimespec.tv_nsec */
		}

		if ((e = gfm_client_compound_begin_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			fatal_metadb_proto("compound_begin request", diag, e);
		else if ((e = gfm_client_put_fd_request(gfm_server, fd))
		    != GFARM_ERR_NO_ERROR)
			fatal_metadb_proto("put_fd request", diag, e);
		else if ((e = close_request(fe)) != GFARM_ERR_NO_ERROR)
			fatal_metadb_proto("close request", diag, e);
		else if ((e = gfm_client_compound_end_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			fatal_metadb_proto("compound_end request", diag, e);

		else if ((e = gfm_client_compound_begin_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			fatal_metadb_proto("compound_begin result", diag, e);
		else if ((e = gfm_client_put_fd_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			fatal_metadb_proto("put_fd result", diag, e);
		else if ((e = close_result(fe)) != GFARM_ERR_NO_ERROR)
			fatal_metadb_proto("close result", diag, e);
		else if ((e = gfm_client_compound_end_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			fatal_metadb_proto("compound_end result", diag, e);

		e2 = file_table_close(fd);
		if (e == GFARM_ERR_NO_ERROR)
			e = e2;
	}

	gfs_server_put_reply(client, diag, e, "");
}

void
gfs_server_pread(struct gfp_xdr *client)
{
	gfarm_int32_t fd, size;
	gfarm_int64_t offset;
	ssize_t rv;
	int save_errno = 0;
	char buffer[GFS_PROTO_MAX_IOSIZE];

	gfs_server_get_request(client, "pread", "iil", &fd, &size, &offset);

	/* We truncatef i/o size bigger than GFS_PROTO_MAX_IOSIZE. */
	if (size > GFS_PROTO_MAX_IOSIZE)
		size = GFS_PROTO_MAX_IOSIZE;
	if ((rv = pread(file_table_get(fd), buffer, size, offset)) == -1)
		save_errno = errno;
	else
		file_table_set_read(fd);

	gfs_server_put_reply_with_errno(client, "pread", rv == -1 ? errno : 0,
	    "b", rv, buffer);
}

void
gfs_server_pwrite(struct gfp_xdr *client)
{
	gfarm_int32_t fd;
	size_t size;
	gfarm_int64_t offset;
	ssize_t rv;
	int save_errno = 0;
	char buffer[GFS_PROTO_MAX_IOSIZE];

	gfs_server_get_request(client, "pwrite", "ibl",
	    &fd, sizeof(buffer), &size, buffer, &offset);

	/*
	 * We truncate i/o size bigger than GFS_PROTO_MAX_IOSIZE.
	 * This is inefficient because passed extra data are just
	 * abandoned. So client should avoid such situation.
	 */
	if (size > GFS_PROTO_MAX_IOSIZE)
		size = GFS_PROTO_MAX_IOSIZE;
	if ((rv = pwrite(file_table_get(fd), buffer, size, offset)) == -1)
		save_errno = errno;
	else
		file_table_set_written(fd);

	gfs_server_put_reply_with_errno(client, "pwrite", save_errno,
	    "i", (gfarm_int32_t)rv);
}

void
gfs_server_ftruncate(struct gfp_xdr *client)
{
	int fd;
	gfarm_int64_t length;
	int save_errno = 0;

	gfs_server_get_request(client, "ftruncate", "il", &fd, &length);

	if (ftruncate(file_table_get(fd), (off_t)length) == -1)
		save_errno = errno;
	else
		file_table_set_written(fd);

	gfs_server_put_reply_with_errno(client, "ftruncate", save_errno, "");
}

void
gfs_server_fsync(struct gfp_xdr *client)
{
	int fd;
	int operation;
	int save_errno = 0;

	gfs_server_get_request(client, "fsync", "ii", &fd, &operation);

	switch (operation) {
	case GFS_PROTO_FSYNC_WITHOUT_METADATA:      
#ifdef HAVE_FDATASYNC
		if (fdatasync(file_table_get(fd)) == -1)
			save_errno = errno;
		break;
#else
		/*FALLTHROUGH*/
#endif
	case GFS_PROTO_FSYNC_WITH_METADATA:
		if (fsync(file_table_get(fd)) == -1)
			save_errno = errno;
		break;
	default:
		save_errno = EINVAL;
		break;
	}

	gfs_server_put_reply_with_errno(client, "fsync", save_errno, "");
}

void
gfs_server_fstat(struct gfp_xdr *client)
{
	struct stat st;
	gfarm_int32_t fd;
	gfarm_off_t size = 0;
	gfarm_int64_t atime_sec = 0, mtime_sec = 0;
	gfarm_int32_t atime_nsec = 0, mtime_nsec = 0;
	int save_errno = 0;

	gfs_server_get_request(client, "fstat", "i", &fd);

	if (fstat(file_table_get(fd), &st) == -1)
		save_errno = errno;
	else {
		size = st.st_size;
		atime_sec = st.st_atime;
		/* XXX FIXME st_atimespec.tv_nsec */
		mtime_sec = st.st_mtime;
		/* XXX FIXME st_mtimespec.tv_nsec */
	}

	gfs_server_put_reply_with_errno(client, "fstat", save_errno,
	    "llili", size, atime_sec, atime_nsec, mtime_sec, mtime_nsec);
}

void
gfs_server_cksum_set(struct gfp_xdr *client)
{
	gfarm_error_t e;
	int fd;
	gfarm_int32_t cksum_len;
	char *cksum_type;
	char cksum[GFM_PROTO_CKSUM_MAXLEN];
	struct file_entry *fe;
	int was_written;
	time_t mtime;
	struct stat st;
	static const char diag[] = "cksum_set";

	gfs_server_get_request(client, diag, "isb", &fd,
	    &cksum_type, sizeof(cksum), &cksum_len, cksum);

	if ((fe = file_table_entry(fd)) == NULL)
		e = GFARM_ERR_BAD_FILE_DESCRIPTOR;
	else {
		/* NOTE: local client could use remote operation as well */
		was_written = (fe->flags & FILE_FLAG_WRITTEN) != 0;
		mtime = fe->mtime;
		if ((fe->flags & FILE_FLAG_LOCAL) == 0) { /* remote? */
			;
		} else if (fstat(fe->local_fd, &st) == -1) {
			gflog_warning("fd %d: stat failed at cksum_set: %s",
			    fd, strerror(errno));
		} else {
			if (st.st_mtime != fe->mtime) {
				mtime = st.st_mtime;
				was_written = 1;
			}
			/* XXX FIXME st_mtimespec.tv_nsec */
		}

		if ((e = gfm_client_compound_begin_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			fatal_metadb_proto("compound_begin request", diag, e);
		else if ((e = gfm_client_put_fd_request(gfm_server, fd))
		    != GFARM_ERR_NO_ERROR)
			fatal_metadb_proto("put_fd request", diag, e);
		else if ((e = gfm_client_cksum_set_request(gfm_server,
		    cksum_type, cksum_len, cksum,
		    was_written, (gfarm_int64_t)mtime, (gfarm_int32_t)0)) !=
		    GFARM_ERR_NO_ERROR)
			fatal_metadb_proto("cksum_set request", diag, e);
		else if ((e = gfm_client_compound_end_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			fatal_metadb_proto("compound_end request", diag, e);

		else if ((e = gfm_client_compound_begin_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			fatal_metadb_proto("compound_begin result", diag, e);
		else if ((e = gfm_client_put_fd_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			fatal_metadb_proto("put_fd result", diag, e);
		else if ((e = gfm_client_cksum_set_result(gfm_server)) !=
		    GFARM_ERR_NO_ERROR)
			fatal_metadb_proto("close result", diag, e);
		else if ((e = gfm_client_compound_end_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			fatal_metadb_proto("compound_end result", diag, e);
	}

	gfs_server_put_reply(client, diag, e, "");
}

#if 0 /* not yet in gfarm v2 */

void
gfs_server_statfs(struct gfp_xdr *client)
{
	char *dir, *path;
	int save_errno = 0;
	gfarm_int32_t bsize;
	gfarm_off_t blocks, bfree, bavail, files, ffree, favail;

	gfs_server_get_request(client, "stafs", "s", &dir);

	local_path(dir, &path, "statfs");
	save_errno = gfsd_statfs(path, &bsize,
	    &blocks, &bfree, &bavail,
	    &files, &ffree, &favail);
	free(path);

	gfs_server_put_reply_with_errno(client, "statfs", save_errno,
	    "ioooooo", bsize, blocks, bfree, bavail, files, ffree, favail);
}

void
gfs_server_bulkread(struct gfp_xdr *client)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	ssize_t rv;
	gfarm_error_t error = GFARM_ERR_NO_ERROR;
	char buffer[GFS_PROTO_MAX_IOSIZE];
	struct gfs_client_rep_rate_info *rinfo = NULL;

	gfs_server_get_request(client, "bulkread", "i", &fd);

	if (file_read_size >= sizeof(buffer))
		file_read_size = sizeof(buffer);
	if (rate_limit != 0) {
		rinfo = gfs_client_rep_rate_info_alloc(rate_limit);
		if (rinfo == NULL)
			fatal("bulkread:rate_info_alloc: %s",
			    gfarm_error_string(GFARM_ERR_NO_MEMORY));
	}

	fd = file_table_get(fd);
	do {
		rv = read(fd, buffer, file_read_size);
		if (rv <= 0) {
			if (rv == -1)
				error = gfarm_errno_to_error(errno);
			break;
		}
		e = gfp_xdr_send(client, "b", rv, buffer);
		if (e != GFARM_ERR_NO_ERROR) {
			error = e;
			break;
		}
		if (file_read_size < GFS_PROTO_MAX_IOSIZE) {
			e = gfp_xdr_flush(client);
			if (e != GFARM_ERR_NO_ERROR) {
				error = e;
				break;
			}
		}
		if (rate_limit != 0)
			gfs_client_rep_rate_control(rinfo, rv);
	} while (rv > 0);

	if (rinfo != NULL)
		gfs_client_rep_rate_info_free(rinfo);
	/* send EOF mark */
	e = gfp_xdr_send(client, "b", 0, buffer);
	if (e != GFARM_ERR_NO_ERROR && error == GFARM_ERR_NO_ERROR)
		error = e;

	gfs_server_put_reply(client, "bulkread", error, "");
}

void
gfs_server_striping_read(struct gfp_xdr *client)
{
	gfarm_error_t e;
	gfarm_int32_t fd, interleave_factor;
	gfarm_off_t offset, size, full_stripe_size;
	gfarm_off_t chunk_size;
	ssize_t rv;
	gfarm_error_t error = GFARM_ERR_NO_ERROR;
	char buffer[GFS_PROTO_MAX_IOSIZE];
	struct gfs_client_rep_rate_info *rinfo = NULL;

	gfs_server_get_request(client, "striping_read", "iooio", &fd,
	    &offset, &size, &interleave_factor, &full_stripe_size);

	if (file_read_size >= sizeof(buffer))
		file_read_size = sizeof(buffer);
	if (rate_limit != 0) {
		rinfo = gfs_client_rep_rate_info_alloc(rate_limit);
		if (rinfo == NULL)
			fatal("striping_read:rate_info_alloc: %s",
			    gfarm_error_string(GFARM_ERR_NO_MEMORY));
	}

	fd = file_table_get(fd);
	if (lseek(fd, (off_t)offset, SEEK_SET) == -1) {
		error = gfarm_errno_to_error(errno);
		goto finish;
	}
	for (;;) {
		chunk_size = interleave_factor == 0 || size < interleave_factor
		    ? size : interleave_factor;
		for (; chunk_size > 0; chunk_size -= rv, size -= rv) {
			rv = read(fd, buffer, chunk_size < file_read_size ?
			    chunk_size : file_read_size);
			if (rv <= 0) {
				if (rv == -1)
					error =
					    gfarm_errno_to_error(errno);
				goto finish;
			}
			e = gfp_xdr_send(client, "b", rv, buffer);
			if (e != GFARM_ERR_NO_ERROR) {
				error = e;
				goto finish;
			}
			if (file_read_size < GFS_PROTO_MAX_IOSIZE) {
				e = gfp_xdr_flush(client);
				if (e != GFARM_ERR_NO_ERROR) {
					error = e;
					goto finish;
				}
			}
			if (rate_limit != 0)
				gfs_client_rep_rate_control(rinfo, rv);
		}
		if (size <= 0)
			break;
		offset += full_stripe_size;
		if (lseek(fd, (off_t)offset, SEEK_SET) == -1) {
			error = gfarm_errno_to_error(errno);
			break;
		}
	}
 finish:
	if (rinfo != NULL)
		gfs_client_rep_rate_info_free(rinfo);
	/* send EOF mark */
	e = gfp_xdr_send(client, "b", 0, buffer);
	if (e != GFARM_ERR_NO_ERROR && error == GFARM_ERR_NO_ERROR)
		error = e;

	gfs_server_put_reply(client, "striping_read", error, "");
}

void
gfs_server_replicate_file_sequential_common(struct gfp_xdr *client,
	char *file, gfarm_int32_t mode,
	char *src_canonical_hostname, char *src_if_hostname)
{
	gfarm_error_t e;
	char *path;
	struct gfs_connection *src_conn;
	int fd, src_fd;
	long file_sync_rate;
	gfarm_error_t error = GFARM_ERR_NO_ERROR;
	struct hostent *hp;
	struct sockaddr_in peer_addr;

	hp = gethostbyname(src_if_hostname);
	free(src_if_hostname);
	if (hp == NULL || hp->h_addrtype != AF_INET) {
		e = GFARM_ERR_UNKNOWN_HOST;
	} else {
		memset(&peer_addr, 0, sizeof(peer_addr));
		memcpy(&peer_addr.sin_addr, hp->h_addr,
		       sizeof(peer_addr.sin_addr));
		peer_addr.sin_family = hp->h_addrtype;
		peer_addr.sin_port = htons(gfarm_spool_server_port);

		e = gfarm_netparam_config_get_long(
		    &gfarm_netparam_file_sync_rate,
		    src_canonical_hostname, (struct sockaddr *)&peer_addr,
		    &file_sync_rate);
		if (e != GFARM_ERR_NO_ERROR) /* shouldn't happen */
			gflog_warning("file_sync_rate: %s",
			    gfarm_error_string(e));

		/*
		 * the following gfs_client_connect() accesses user & home
		 * information which was set in gfarm_authorize()
		 * with switch_to==1.
		 */
		e = gfs_client_connect(
		    src_canonical_hostname, (struct sockaddr *)&peer_addr,
		    &src_conn);
	}
	free(src_canonical_hostname);
	if (e != GFARM_ERR_NO_ERROR) {
		error = e;
		gflog_warning("replicate_file_seq:remote_connect: %s",
		    gfarm_error_string(e));
	} else {
		e = gfs_client_open(src_conn, file, GFARM_FILE_RDONLY, 0,
				    &src_fd);
		if (e != GFARM_ERR_NO_ERROR) {
			error = e;
			gflog_warning("replicate_file_seq:remote_open: %s",
			    gfarm_error_string(e));
		} else {
			e = gfarm_path_localize(file, &path);
			if (e != GFARM_ERR_NO_ERROR)
				fatal("replicate_file_seq:path: %s",
				    gfarm_error_string(e));
			fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, mode);
			if (fd < 0) {
				error = gfarm_errno_to_error(errno);
				gflog_warning_errno(
				    "replicate_file_seq:local_open");
			} else {
				e = gfs_client_copyin(src_conn, src_fd, fd,
				    file_sync_rate);
				if (e != GFARM_ERR_NO_ERROR) {
					error = e;
					gflog_warning(
					    "replicate_file_seq:copyin: %s",
					    gfarm_error_string(e));
				}
				close(fd);
			}
			e = gfs_client_close(src_conn, src_fd);
			free(path);
		}
		gfs_client_disconnect(src_conn);
	}
	free(file);

	gfs_server_put_reply(client, "replicate_file_seq", error, "");
}

/* obsolete interafce, keeped for backward compatibility */
void
gfs_server_replicate_file_sequential_old(struct gfp_xdr *client)
{
	char *file, *src_canonical_hostname, *src_if_hostname;
	gfarm_int32_t mode;

	gfs_server_get_request(client, "replicate_file_seq_old",
	    "sis", &file, &mode, &src_if_hostname);

	src_canonical_hostname = strdup(src_if_hostname);
	if (src_canonical_hostname == NULL) {
		gfs_server_put_reply(client, "replicate_file_seq_old",
		    GFARM_ERR_NO_MEMORY, "");
		return;
	}
	gfs_server_replicate_file_sequential_common(client, file, mode,
	    src_canonical_hostname, src_if_hostname);
}

void
gfs_server_replicate_file_sequential(struct gfp_xdr *client)
{
	char *file, *src_canonical_hostname, *src_if_hostname;
	gfarm_int32_t mode;

	gfs_server_get_request(client, "replicate_file_seq",
	    "siss", &file, &mode, &src_canonical_hostname, &src_if_hostname);

	gfs_server_replicate_file_sequential_common(client, file, mode,
	    src_canonical_hostname, src_if_hostname);
}

int iosize_alignment = 4096;
int iosize_minimum_division = 65536;

struct parallel_stream {
	struct gfs_connection *src_conn;
	int src_fd;
	enum { GSRFP_COPYING, GSRFP_FINISH } state;
};

gfarm_error_t
simple_division(int ofd, struct parallel_stream *divisions,
	off_t file_size, int n)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	gfarm_off_t offset = 0, residual = file_size;
	gfarm_off_t size_per_division = file_size / n;
	int i;

	if ((size_per_division / iosize_alignment) *
	    iosize_alignment != size_per_division) {
		size_per_division =
		    ((size_per_division / iosize_alignment) + 1) *
		    iosize_alignment;
	}

	for (i = 0; i < n; i++) {
		gfarm_off_t size;

		if (residual <= 0 || e_save != GFARM_ERR_NO_ERROR) {
			divisions[i].state = GSRFP_FINISH;
			continue;
		}
		size = residual <= size_per_division ?
		    residual : size_per_division;
		e = gfs_client_striping_copyin_request(
		    divisions[i].src_conn, divisions[i].src_fd, ofd,
		    offset, size, 0, 0);
		offset += size_per_division;
		residual -= size;
		if (e != GFARM_ERR_NO_ERROR) {
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
			gflog_warning("replicate_file_division:copyin: %s",
			    gfarm_error_string(e));
			divisions[i].state = GSRFP_FINISH;
			continue;
		}
		divisions[i].state = GSRFP_COPYING;
	}
	return (e_save);
}

gfarm_error_t
striping(int ofd, struct parallel_stream *divisions,
	off_t file_size, int n, int interleave_factor)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	gfarm_off_t full_stripe_size = (gfarm_off_t)interleave_factor * n;
	gfarm_off_t stripe_number = file_size / full_stripe_size;
	gfarm_off_t size_per_division = interleave_factor * stripe_number;
	gfarm_off_t residual = file_size - full_stripe_size * stripe_number;
	gfarm_off_t chunk_number_on_last_stripe;
	gfarm_off_t last_chunk_size;
	gfarm_off_t offset = 0;
	int i;

	if (residual == 0) {
		chunk_number_on_last_stripe = 0;
		last_chunk_size = 0;
	} else {
		chunk_number_on_last_stripe = residual / interleave_factor;
		last_chunk_size = residual - 
		    interleave_factor * chunk_number_on_last_stripe;
	}

	for (i = 0; i < n; i++) {
		gfarm_off_t size = size_per_division;

		if (i < chunk_number_on_last_stripe)
			size += interleave_factor;
		else if (i == chunk_number_on_last_stripe)
			size += last_chunk_size;
		if (size <= 0 || e_save != GFARM_ERR_NO_ERROR) {
			divisions[i].state = GSRFP_FINISH;
			continue;
		}
		e = gfs_client_striping_copyin_request(
		    divisions[i].src_conn, divisions[i].src_fd, ofd,
		    offset, size, interleave_factor, full_stripe_size);
		offset += interleave_factor;
		if (e != GFARM_ERR_NO_ERROR) {
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
			gflog_warning("replicate_file_stripe:copyin: %s",
			    gfarm_error_string(e));
			divisions[i].state = GSRFP_FINISH;
			continue;
		}
		divisions[i].state = GSRFP_COPYING;
	}
	return (e_save);
}

void
limit_division(int *ndivisionsp, gfarm_off_t file_size)
{
	int ndivisions = *ndivisionsp;

	/* do not divide too much */
	if (ndivisions > file_size / iosize_minimum_division) {
		ndivisions = file_size / iosize_minimum_division;
		if (ndivisions == 0)
			ndivisions = 1;
	}
	*ndivisionsp = ndivisions;
}

void
gfs_server_replicate_file_parallel_common(struct gfp_xdr *client,
	char *file, gfarm_int32_t mode, gfarm_off_t file_size,
	gfarm_int32_t ndivisions, gfarm_int32_t interleave_factor,
	char *src_canonical_hostname, char *src_if_hostname)
{
	struct parallel_stream *divisions;
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	char *path;
	long file_sync_rate, written;
	int i, j, n, ofd;
	gfarm_error_t error = GFARM_ERR_NO_ERROR;
	struct hostent *hp;
	struct sockaddr_in peer_addr;

	e = gfarm_path_localize(file, &path);
	if (e != GFARM_ERR_NO_ERROR)
		fatal("replicate_file_par: %s", gfarm_error_string(e));
	ofd = open(path, O_WRONLY|O_CREAT|O_TRUNC, mode);
	free(path);
	if (ofd == -1) {
		error = gfarm_errno_to_error(errno);
		gflog_warning_errno("replicate_file_par:local_open");
		goto finish;
	}

	limit_division(&ndivisions, file_size);

	divisions = malloc(sizeof(*divisions) * ndivisions);
	if (divisions == NULL) {
		error = GFARM_ERR_NO_MEMORY;
		goto finish_ofd;
	}

	hp = gethostbyname(src_if_hostname);
	if (hp == NULL || hp->h_addrtype != AF_INET) {
		error = GFARM_ERR_CONNECTION_REFUSED;
		goto finish_free_divisions;
	}
	memset(&peer_addr, 0, sizeof(peer_addr));
	memcpy(&peer_addr.sin_addr, hp->h_addr, sizeof(peer_addr.sin_addr));
	peer_addr.sin_family = hp->h_addrtype;
	peer_addr.sin_port = htons(gfarm_spool_server_port);

	e = gfarm_netparam_config_get_long(&gfarm_netparam_file_sync_rate,
	    src_canonical_hostname, (struct sockaddr *)&peer_addr,
	    &file_sync_rate);
	if (e != GFARM_ERR_NO_ERROR) /* shouldn't happen */
		gflog_warning("file_sync_rate: %s", gfarm_error_string(e));

	/* XXX - this should be done in parallel rather than sequential */
	for (i = 0; i < ndivisions; i++) {

		e = gfs_client_connect(
		    src_canonical_hostname, (struct sockaddr *)&peer_addr,
		    &divisions[i].src_conn);
		if (e != GFARM_ERR_NO_ERROR) {
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
			gflog_warning("replicate_file_par:remote_connect: %s",
			    gfarm_error_string(e));
			break;
		}
	}
	n = i;
	if (n == 0) {
		error = e;
		goto finish_free_divisions;
	}
	e_save = GFARM_ERR_NO_ERROR; /* not fatal */

	/* XXX - this should be done in parallel rather than sequential */
	for (i = 0; i < n; i++) {
		e = gfs_client_open(divisions[i].src_conn, file,
		    GFARM_FILE_RDONLY, 0, &divisions[i].src_fd);
		if (e != GFARM_ERR_NO_ERROR) {
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
			gflog_warning("replicate_file_par:remote_open: %s",
			    gfarm_error_string(e));

			/*
			 * XXX - this should be done in parallel
			 * rather than sequential
			 */
			for (j = i; j < n; j++)
				gfs_client_disconnect(divisions[j].src_conn);
			n = i;
			break;
		}
	}
	if (n == 0) {
		error = e_save;
		goto finish_free_divisions;
	}
	e_save = GFARM_ERR_NO_ERROR; /* not fatal */

	if (interleave_factor == 0) {
		e = simple_division(ofd, divisions, file_size, n);
	} else {
		e = striping(ofd, divisions, file_size, n, interleave_factor);
	}
	e_save = e;

	written = 0;
	/*
	 * XXX - we cannot stop here, even if e_save != GFARM_ERR_NO_ERROR,
	 * because currently there is no way to cancel
	 * striping_copyin request.
	 */
	for (;;) {
		int max_fd, fd, nfound, rv;
		fd_set readable;

		FD_ZERO(&readable);
		max_fd = -1;
		for (i = 0; i < n; i++) {
			if (divisions[i].state != GSRFP_COPYING)
				continue;
			fd = gfs_client_connection_fd(divisions[i].src_conn);
			/* XXX - prevent this happens */
			if (fd >= FD_SETSIZE) {
				fatal("replicate_file_par: "
				    "too big file descriptor");
			}
			FD_SET(fd, &readable);
			if (max_fd < fd)
				max_fd = fd;
		}
		if (max_fd == -1)
			break;
		nfound = select(max_fd + 1, &readable, NULL, NULL, NULL);
		if (nfound <= 0) {
			if (nfound == -1 && errno != EINTR && errno != EAGAIN)
				gflog_warning_errno(
				    "replicate_file_par:select");
			continue;
		}
		for (i = 0; i < n; i++) {
			if (divisions[i].state != GSRFP_COPYING)
				continue;
			fd = gfs_client_connection_fd(divisions[i].src_conn);
			if (!FD_ISSET(fd, &readable))
				continue;
			e = gfs_client_striping_copyin_partial(
			    divisions[i].src_conn, &rv);
			if (e != GFARM_ERR_NO_ERROR) {
				if (e_save == GFARM_ERR_NO_ERROR)
					e_save = e;
				divisions[i].state = GSRFP_FINISH; /* XXX */
			} else if (rv == 0) {
				divisions[i].state = GSRFP_FINISH;
				e = gfs_client_striping_copyin_result(
				    divisions[i].src_conn);
				if (e != GFARM_ERR_NO_ERROR) {
					if (e_save == GFARM_ERR_NO_ERROR)
						e_save = e;
				} else {
					e = gfs_client_close(
					    divisions[i].src_conn,
					    divisions[i].src_fd);
					if (e_save == GFARM_ERR_NO_ERROR)
						e_save = e;
				}
			} else if (file_sync_rate != 0) {
				written += rv;
				if (written >= file_sync_rate) {
					written -= file_sync_rate;
#ifdef HAVE_FDATASYNC
					fdatasync(ofd);
#else
					fsync(ofd);
#endif
				}
			}
			if (--nfound <= 0)
				break;
		}
	}

	/* XXX - this should be done in parallel rather than sequential */
	for (i = 0; i < n; i++) {
		e = gfs_client_disconnect(divisions[i].src_conn);
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
	}
	if (e_save != GFARM_ERR_NO_ERROR)
		error = e_save;

finish_free_divisions:
	free(divisions);
finish_ofd:
	close(ofd);
finish:
	free(file);
	free(src_canonical_hostname);
	free(src_if_hostname);
	gfs_server_put_reply(client, "replicate_file_par", error, "");
}

/* obsolete interafce, keeped for backward compatibility */
void
gfs_server_replicate_file_parallel_old(struct gfp_xdr *client)
{
	char *file, *src_canonical_hostname, *src_if_hostname;
	gfarm_int32_t mode;
	gfarm_int32_t ndivisions; /* parallel_streams */
	gfarm_int32_t interleave_factor; /* stripe_unit_size, chuck size */
	gfarm_off_t file_size;

	gfs_server_get_request(client, "replicate_file_par_old", "sioiis",
	    &file, &mode, &file_size, &ndivisions, &interleave_factor,
	    &src_if_hostname);

	src_canonical_hostname = strdup(src_if_hostname);
	if (src_canonical_hostname == NULL) {
		gfs_server_put_reply(client, "replicate_file_par_old",
		    GFARM_ERR_NO_MEMORY, "");
		return;
	}
	gfs_server_replicate_file_parallel_common(client,
	    file, mode, file_size, ndivisions, interleave_factor,
	    src_canonical_hostname, src_if_hostname);
}

void
gfs_server_replicate_file_parallel(struct gfp_xdr *client)
{
	char *file, *src_canonical_hostname, *src_if_hostname;
	gfarm_int32_t mode;
	gfarm_int32_t ndivisions; /* parallel_streams */
	gfarm_int32_t interleave_factor; /* stripe_unit_size, chuck size */
	gfarm_off_t file_size;

	gfs_server_get_request(client, "replicate_file_par", "sioiiss",
	    &file, &mode, &file_size, &ndivisions, &interleave_factor,
	    &src_canonical_hostname, &src_if_hostname);

	gfs_server_replicate_file_parallel_common(client,
	    file, mode, file_size, ndivisions, interleave_factor,
	    src_canonical_hostname, src_if_hostname);
}

void
gfs_server_chdir(struct gfp_xdr *client)
{
	char *gpath, *path;
	int save_errno = 0;

	gfs_server_get_request(client, "chdir", "s", &gpath);

	local_path(gpath, &path, "chdir");
	if (chdir(path) == -1)
		save_errno = errno;
	free(path);

	gfs_server_put_reply_with_errno(client, "chdir", save_errno, "");
}

struct gfs_server_command_context {
	struct gfarm_iobuffer *iobuffer[NFDESC];

	enum { GFS_COMMAND_SERVER_STATE_NEUTRAL,
		       GFS_COMMAND_SERVER_STATE_OUTPUT,
		       GFS_COMMAND_SERVER_STATE_EXITED }
		server_state;
	int server_output_fd;
	int server_output_residual;
	enum { GFS_COMMAND_CLIENT_STATE_NEUTRAL,
		       GFS_COMMAND_CLIENT_STATE_OUTPUT }
		client_state;
	int client_output_residual;

	int pid;
	int exited_pid;
	int status;
} server_command_context;

#define COMMAND_IS_RUNNING()	(server_command_context.exited_pid == 0)

volatile sig_atomic_t sigchld_jmp_needed = 0;
sigjmp_buf sigchld_jmp_buf;

void
sigchld_handler(int sig)
{
	int pid, status;

	for (;;) {
		pid = waitpid(-1, &status, WNOHANG);
		if (pid == -1 || pid == 0)
			break;
		server_command_context.exited_pid = pid;
		server_command_context.status = status;
	}
	if (sigchld_jmp_needed) {
		sigchld_jmp_needed = 0;
		siglongjmp(sigchld_jmp_buf, 1);
	}
}

void fatal_command(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
void
fatal_command(const char *format, ...)
{
	va_list ap;
	struct gfs_server_command_context *cc = &server_command_context;

	/* "-" is to send it to the process group */
	kill(-cc->pid, SIGTERM);

	va_start(ap, format);
	vfatal(format, ap);
	va_end(ap);
}

char *
gfs_server_command_fd_set(struct gfp_xdr *client,
			  fd_set *readable, fd_set *writable, int *max_fdp)
{
	struct gfs_server_command_context *cc = &server_command_context;
	int conn_fd = gfp_xdr_fd(client);
	int i, fd;

	/*
	 * The following test condition should just match with
	 * the i/o condition in gfs_server_command_io_fd_set(),
	 * otherwise unneeded busy wait happens.
	 */

	if (cc->client_state == GFS_COMMAND_CLIENT_STATE_NEUTRAL ||
	    (cc->client_state == GFS_COMMAND_CLIENT_STATE_OUTPUT &&
	     gfarm_iobuffer_is_readable(cc->iobuffer[FDESC_STDIN]))) {
		FD_SET(conn_fd, readable);
		if (*max_fdp < conn_fd)
			*max_fdp = conn_fd;
	}
	if ((cc->server_state == GFS_COMMAND_SERVER_STATE_NEUTRAL &&
	     (gfarm_iobuffer_is_writable(cc->iobuffer[FDESC_STDERR]) ||
	      gfarm_iobuffer_is_writable(cc->iobuffer[FDESC_STDOUT]) ||
	      !COMMAND_IS_RUNNING())) ||
	    cc->server_state == GFS_COMMAND_SERVER_STATE_OUTPUT) {
		FD_SET(conn_fd, writable);
		if (*max_fdp < conn_fd)
			*max_fdp = conn_fd;
	}

	if (COMMAND_IS_RUNNING() &&
	    gfarm_iobuffer_is_writable(cc->iobuffer[FDESC_STDIN])) {
		fd = gfarm_iobuffer_get_write_fd(cc->iobuffer[FDESC_STDIN]);
		FD_SET(fd, writable);
		if (*max_fdp < fd)
			*max_fdp = fd;
	}

	for (i = FDESC_STDOUT; i <= FDESC_STDERR; i++) {
		if (gfarm_iobuffer_is_readable(cc->iobuffer[i])) {
			fd = gfarm_iobuffer_get_read_fd(cc->iobuffer[i]);
			FD_SET(fd, readable);
			if (*max_fdp < fd)
				*max_fdp = fd;
		}
	}
	return (NULL);
}

gfarm_error_t
gfs_server_command_io_fd_set(struct gfp_xdr *client,
			     fd_set *readable, fd_set *writable)
{
	gfarm_error_t e;
	struct gfs_server_command_context *cc = &server_command_context;
	int i, fd, conn_fd = gfp_xdr_fd(client);

	fd = gfarm_iobuffer_get_write_fd(cc->iobuffer[FDESC_STDIN]);
	if (FD_ISSET(fd, writable)) {
		assert(gfarm_iobuffer_is_writable(cc->iobuffer[FDESC_STDIN]));
		gfarm_iobuffer_write(cc->iobuffer[FDESC_STDIN], NULL);
		e = gfarm_iobuffer_get_error(cc->iobuffer[FDESC_STDIN]);
		if (e != GFARM_ERR_NO_ERROR) {
			/* just purge the content */
			gfarm_iobuffer_purge(cc->iobuffer[FDESC_STDIN], NULL);
			gflog_warning("command: abandon stdin: %s",
			    gfarm_error_string(e));
			gfarm_iobuffer_set_error(cc->iobuffer[FDESC_STDIN],
			    GFARM_ERR_NO_ERROR);
		}
		if (gfarm_iobuffer_is_eof(cc->iobuffer[FDESC_STDIN]))
			close(fd);
	}

	for (i = FDESC_STDOUT; i <= FDESC_STDERR; i++) {
		fd = gfarm_iobuffer_get_read_fd(cc->iobuffer[i]);
		if (!FD_ISSET(fd, readable))
			continue;
		gfarm_iobuffer_read(cc->iobuffer[i], NULL);
		e = gfarm_iobuffer_get_error(cc->iobuffer[i]);
		if (e == GFARM_ERR_NO_ERROR)
			continue;
		/* treat this as eof */
		gfarm_iobuffer_set_read_eof(cc->iobuffer[i]);
		gflog_warning("%s: %s", i == FDESC_STDOUT ?
		    "command: reading stdout" :
		    "command: reading stderr",
		     gfarm_error_string(e));
		gfarm_iobuffer_set_error(cc->iobuffer[i], GFARM_ERR_NO_ERROR);
	}

	if (FD_ISSET(conn_fd, readable) &&
	    cc->server_state != GFS_COMMAND_SERVER_STATE_EXITED) {
		if (cc->client_state == GFS_COMMAND_CLIENT_STATE_NEUTRAL) {
			gfarm_int32_t cmd, fd, len, sig;
			int eof;

			e = gfp_xdr_recv(client, 1, &eof, "i", &cmd);
			if (e != GFARM_ERR_NO_ERROR)
				fatal_command("command:client subcommand");
			if (eof)
				fatal_command("command:client subcommand: "
				    "eof");
			switch (cmd) {
			case GFS_PROTO_COMMAND_EXIT_STATUS:
				fatal_command("command:client subcommand: "
				    "unexpected exit_status");
				break;
			case GFS_PROTO_COMMAND_SEND_SIGNAL:
				e = gfp_xdr_recv(client, 1, &eof, "i", &sig);
				if (e != GFARM_ERR_NO_ERROR)
					fatal_command(
					    "command_send_signal: %s",
					    gfarm_error_string(e));
				if (eof)
					fatal_command(
					    "command_send_signal: eof");
				/* "-" is to send it to the process group */
				kill(-cc->pid, sig);
				break;
			case GFS_PROTO_COMMAND_FD_INPUT:
				e = gfp_xdr_recv(client, 1, &eof,
						   "ii", &fd, &len);
				if (e != GFARM_ERR_NO_ERROR)
					fatal_command("command_fd_input: %s",
					    gfarm_error_string(e));
				if (eof)
					fatal_command("command_fd_input: eof");
				if (fd != FDESC_STDIN) {
					/* XXX - something wrong */
					fatal_command("command_fd_input: fd");
				}
				if (len <= 0) {
					/* notify closed */
					gfarm_iobuffer_set_read_eof(
					    cc->iobuffer[FDESC_STDIN]);
				} else {
					cc->client_state =
					    GFS_COMMAND_CLIENT_STATE_OUTPUT;
					cc->client_output_residual = len;
				}
				break;
			default:
				/* XXX - something wrong */
				fatal_command("command_io: "
				    "unknown subcommand");
				break;
			}
		} else if (cc->client_state==GFS_COMMAND_CLIENT_STATE_OUTPUT) {
			gfarm_iobuffer_read(cc->iobuffer[FDESC_STDIN],
				&cc->client_output_residual);
			if (cc->client_output_residual == 0)
				cc->client_state =
					GFS_COMMAND_CLIENT_STATE_NEUTRAL;
			e = gfarm_iobuffer_get_error(
			    cc->iobuffer[FDESC_STDIN]);
			if (e != GFARM_ERR_NO_ERROR) {
				/* treat this as eof */
				gfarm_iobuffer_set_read_eof(
				    cc->iobuffer[FDESC_STDIN]);
				gflog_warning("command: receiving stdin: %s",
				    gfarm_error_string(e));
				gfarm_iobuffer_set_error(
				    cc->iobuffer[FDESC_STDIN],
				    GFARM_ERR_NO_ERROR);
			}
			if (gfarm_iobuffer_is_read_eof(
					cc->iobuffer[FDESC_STDIN])) {
				fatal_command("command_fd_input_content: eof");
			}
		}
	}
	if (FD_ISSET(conn_fd, writable)) {
		if (cc->server_state == GFS_COMMAND_SERVER_STATE_NEUTRAL) {
			if (gfarm_iobuffer_is_writable(
				cc->iobuffer[FDESC_STDERR]) ||
			    gfarm_iobuffer_is_writable(
				cc->iobuffer[FDESC_STDOUT])) {
				if (gfarm_iobuffer_is_writable(
						cc->iobuffer[FDESC_STDERR]))
					cc->server_output_fd = FDESC_STDERR;
				else
					cc->server_output_fd = FDESC_STDOUT;
				/*
				 * cc->server_output_residual may be 0,
				 * if stdout or stderr is closed.
				 */
				cc->server_output_residual =
				    gfarm_iobuffer_avail_length(
					cc->iobuffer[cc->server_output_fd]);
				e = gfp_xdr_send(client, "iii",
					GFS_PROTO_COMMAND_FD_OUTPUT,
					cc->server_output_fd,
					cc->server_output_residual);
				if (e != GFARM_ERR_NO_ERROR ||
				    (e = gfp_xdr_flush(client)) !=
				     GFARM_ERR_NO_ERROR)
					fatal_command("command: fd_output: %s",
					    gfarm_error_string(e));
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_OUTPUT;
			} else if (!COMMAND_IS_RUNNING()) {
				e = gfp_xdr_send(client, "i",
					GFS_PROTO_COMMAND_EXITED);
				if (e != GFARM_ERR_NO_ERROR ||
				    (e = gfp_xdr_flush(client)) !=
				    GFARM_ERR_NO_ERROR)
					fatal("command: report exit: %s",
					    gfarm_error_string(e));
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_EXITED;
			}
		} else if (cc->server_state==GFS_COMMAND_SERVER_STATE_OUTPUT) {
			gfarm_iobuffer_write(
				cc->iobuffer[cc->server_output_fd],
				&cc->server_output_residual);
			if (cc->server_output_residual == 0)
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_NEUTRAL;
			e = gfarm_iobuffer_get_error(
			    cc->iobuffer[cc->server_output_fd]);
			if (e != GFARM_ERR_NO_ERROR) {
				fatal_command("command: sending %s: %s",
				    cc->server_output_fd == FDESC_STDOUT ?
				    "stdout" : "stderr",
				    gfarm_error_string(e));
			}
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_server_command_io(struct gfp_xdr *client, struct timeval *timeout)
{
	volatile int nfound;
	int max_fd, conn_fd = gfp_xdr_fd(client);
	fd_set readable, writable;
	gfarm_error_t e;

	if (server_command_context.server_state ==
	    GFS_COMMAND_SERVER_STATE_EXITED)
		return (GFARM_ERR_NO_ERROR);

	max_fd = -1;
	FD_ZERO(&readable);
	FD_ZERO(&writable);

	gfs_server_command_fd_set(client, &readable, &writable, &max_fd);

	/*
	 * We wait for either SIGCHLD or select(2) event here,
	 * and use siglongjmp(3) to avoid a race condition about the signal.
	 *
	 * The race condition happens, if the SIGCHLD signal is delivered
	 * after the if-statement which does FD_SET(conn_fd, writable) in
	 * gfs_server_command_fd_set(), and before the select(2) below.
	 * In that condition, the following select(2) may wait that the
	 * `conn_fd' becomes readable, and because it may never happan,
	 * it waits forever (i.e. both gfrcmd and gfsd wait each other
	 * forever), and hangs, unless there is the sigsetjmp()/siglongjmp()
	 * handling.
	 *
	 * If the SIGCHLD is delivered inside the select(2) system call,
	 * the problem doesn't happen, because select(2) will return
	 * with EINTR.
	 *
	 * Also, if the SIGCHLD is delivered before an EOF from either
	 * cc->iobuffer[FDESC_STDOUT] or cc->iobuffer[FDESC_STDERR],
	 * the problem doesn't happen, either. Because the select(2)
	 * will be woken up by the EOF. But actually the SIGCHLD is
	 * delivered after the EOF (of both FDESC_STDOUT and FDESC_STDERR,
	 * and even after the EOFs are reported to a client) at least
	 * on linux-2.4.21-pre4.
	 */
	nfound = -1; errno = EINTR;
	if (sigsetjmp(sigchld_jmp_buf, 1) == 0) {
		sigchld_jmp_needed = 1;
		/*
		 * Here, we have to wait until the `conn_fd' is writable,
		 * if this is !COMMAND_IS_RUNNING() case.
		 */
		if (COMMAND_IS_RUNNING() || FD_ISSET(conn_fd, &writable)) {
			nfound = select(max_fd + 1, &readable, &writable, NULL,
			    timeout);
		}
	}
	sigchld_jmp_needed = 0;

	if (nfound > 0)
		e = gfs_server_command_io_fd_set(client, &readable, &writable);
	else
		e = GFARM_ERR_NO_ERROR;

	return (e);
}

gfarm_error_t
gfs_server_client_command_result(struct gfp_xdr *client)
{
	struct gfs_server_command_context *cc = &server_command_context;
	gfarm_int32_t cmd, fd, len, sig;
	int finish, eof;
	gfarm_error_t e;

	while (cc->server_state != GFS_COMMAND_SERVER_STATE_EXITED)
		gfs_server_command_io(client, NULL);
	/*
	 * Because COMMAND_IS_RUNNING() must be false here,
	 * we don't have to call fatal_command() from now on.
	 */

	/*
	 * Now, we recover the connection file descriptor blocking mode.
	 */
	if (fcntl(gfp_xdr_fd(client), F_SETFL, 0) == -1)
		gflog_warning("command-result:block: %s", strerror(errno));

	/* make cc->client_state neutral */
	if (cc->client_state == GFS_COMMAND_CLIENT_STATE_OUTPUT) {
		e = gfp_xdr_purge(client, 0, cc->client_output_residual);
		if (e != GFARM_ERR_NO_ERROR)
			fatal("command_fd_input:purge: %s",
			    gfarm_error_string(e));
	}

	for (finish = 0; !finish; ) {
		e = gfp_xdr_recv(client, 0, &eof, "i", &cmd);
		if (e != GFARM_ERR_NO_ERROR)
			fatal("command:client subcommand: %s",
			    gfarm_error_string(e));
		if (eof)
			fatal("command:client subcommand: eof");
		switch (cmd) {
		case GFS_PROTO_COMMAND_EXIT_STATUS:
			finish = 1;
			break;
		case GFS_PROTO_COMMAND_SEND_SIGNAL:
			e = gfp_xdr_recv(client, 0, &eof, "i", &sig);
			if (e != GFARM_ERR_NO_ERROR)
				fatal("command_send_signal: %s",
				    gfarm_error_string(e));
			if (eof)
				fatal("command_send_signal: eof");
			break;
		case GFS_PROTO_COMMAND_FD_INPUT:
			e = gfp_xdr_recv(client, 0, &eof, "ii", &fd, &len);
			if (e != GFARM_ERR_NO_ERROR)
				fatal("command_fd_input: %s",
				    gfarm_error_string(e));
			if (eof)
				fatal("command_fd_input: eof");
			if (fd != FDESC_STDIN) {
				/* XXX - something wrong */
				fatal("command_fd_input: fd");
			}
			e = gfp_xdr_purge(client, 0, len);
			if (e != GFARM_ERR_NO_ERROR)
				fatal("command_fd_input:purge: %s",
				    gfarm_error_string(e));
			break;
		default:
			/* XXX - something wrong */
			fatal("command_io: unknown subcommand %d", (int)cmd);
			break;
		}
	}
	gfs_server_put_reply(client, "command:exit_status",
	    GFARM_ERR_NO_ERROR, "iii",
	    WIFEXITED(cc->status) ? 0 : WTERMSIG(cc->status),
	    WIFEXITED(cc->status) ? WEXITSTATUS(cc->status) : 0,
	    WIFEXITED(cc->status) ? 0 : WCOREDUMP(cc->status));
	return (GFARM_ERR_NO_ERROR);
}

void
gfs_server_command(struct gfp_xdr *client, char *cred_env)
{
	struct gfs_server_command_context *cc = &server_command_context;
	gfarm_int32_t argc, nenv, flags, error;
	char *path, *command, **argv_storage, **argv, **envp, *xauth;
	int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
	int conn_fd = gfp_xdr_fd(client);
	int i, eof;
	socklen_t siz;
	struct passwd *pw;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	char *user, *home, *shell;
	char *user_env, *home_env, *shell_env, *xauth_env; /* cred_end */
	static char user_format[] = "USER=%s";
	static char home_format[] = "HOME=%s";
	static char shell_format[] = "SHELL=%s";
	static char path_env[] = GFARM_DEFAULT_PATH;
#define N_EXTRA_ENV	4	/* user_env, home_env, shell_env, path_env */
	int use_cred_env = cred_env != NULL ? 1 : 0;

	static char xauth_format[] = "XAUTHORITY=%s";
	static char xauth_template[] = "/tmp/.xaXXXXXX";
	static char xauth_filename[sizeof(xauth_template)];
	int use_xauth_env = 0;

#ifdef __GNUC__ /* workaround gcc warning: unused variable */
	envp = NULL; xauth_env = NULL;
#endif
	gfs_server_get_request(client, "command", "siii",
			       &path, &argc, &nenv, &flags);
	if ((flags & GFS_CLIENT_COMMAND_FLAG_SHELL_COMMAND) != 0) {
		/* "$SHELL" + "-c" */
		argv_storage = malloc(sizeof(char *) * (argc + 3));
		argv = argv_storage + 2;
	} else {
		argv_storage = malloc(sizeof(char *) * (argc + 1));
		argv = argv_storage;
	}
	if (argv_storage == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto rpc_error;
	}

	if ((flags & GFS_CLIENT_COMMAND_FLAG_XAUTHCOPY) != 0)
		use_xauth_env = 1;
	envp = malloc(sizeof(char *) *
	    (nenv + N_EXTRA_ENV + use_cred_env + use_xauth_env + 1));
	if (envp == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_argv;
	}

	user = gfarm_get_local_username();
	home = gfarm_get_local_homedir();
	user_env = malloc(sizeof(user_format) + strlen(user));
	if (user_env == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_envp;
	}
	sprintf(user_env, user_format, user);

	home_env = malloc(sizeof(home_format) + strlen(home));
	if (home_env == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_user_env;
	}
	sprintf(home_env, home_format, home);

	pw = getpwnam(user);
	if (pw == NULL)
		fatal("%s: user doesn't exist", user);
	shell = pw->pw_shell;
	if (*shell == '\0')
		shell = PATH_BSHELL;

	if ((flags & GFS_CLIENT_COMMAND_FLAG_SHELL_COMMAND) == 0) {
		/*
		 * SECURITY.
		 * disallow anyone who doesn't have a standard shell.
		 */
		char *s;

		while ((s = getusershell()) != NULL)
			if (strcmp(s, shell) == 0)
				break;
		endusershell();
		if (s == NULL) {
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			goto free_home_env;
		}
	}

	shell_env = malloc(sizeof(shell_format) + strlen(shell));
	if (shell_env == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_home_env;
	}
	sprintf(shell_env, shell_format, shell);

	argv[argc] = envp[nenv + N_EXTRA_ENV + use_cred_env + use_xauth_env] =
	    NULL;
	envp[nenv + 0] = user_env;
	envp[nenv + 1] = home_env;
	envp[nenv + 2] = shell_env;
	envp[nenv + 3] = path_env;

	for (i = 0; i < argc; i++) {
		e = gfp_xdr_recv(client, 0, &eof, "s", &argv[i]);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			while (--i >= 0)
				free(argv[i]);
			goto free_shell_env;
		}
	}
	for (i = 0; i < nenv; i++) {
		e = gfp_xdr_recv(client, 0, &eof, "s", &envp[i]);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			while (--i >= 0)
				free(envp[i]);
			goto free_argv_array;
		}
	}
	if ((flags & GFS_CLIENT_COMMAND_FLAG_SHELL_COMMAND) != 0) {
		argv_storage[0] = shell;
		argv_storage[1] = "-c";
		argv[1] = NULL; /* ignore argv[1 ... argc - 1] in this case. */
		command = shell;
	} else {
		command = path;
	}
	if (use_cred_env)
		envp[nenv + N_EXTRA_ENV] = cred_env;
	if (use_xauth_env) {
		static char xauth_command_format[] =
			"%s %s nmerge - 2>/dev/null";
		char *xauth_command;
		FILE *fp;
		int xauth_fd;

		e = gfp_xdr_recv(client, 0, &eof, "s", &xauth);
		if (e != GFARM_ERR_NO_ERROR || eof)
			goto free_envp_array;

		/*
		 * don't touch $HOME/.Xauthority to avoid lock contention
		 * on NFS home. (Is this really better? XXX)
		 */
		xauth_fd = mkstemp(strcpy(xauth_filename, xauth_template));
		if (xauth_fd == -1)
			goto free_xauth;
		close(xauth_fd);

		xauth_env = malloc(sizeof(xauth_format) +
				   sizeof(xauth_filename));
		if (xauth_env == NULL)
			goto remove_xauth;
		sprintf(xauth_env, xauth_format, xauth_filename);
		envp[nenv + N_EXTRA_ENV + use_cred_env] = xauth_env;

		xauth_command = malloc(sizeof(xauth_command_format) +
				       strlen(xauth_env) +
				       strlen(XAUTH_COMMAND));
		if (xauth_command == NULL)
			goto free_xauth_env;
		sprintf(xauth_command, xauth_command_format,
			xauth_env, XAUTH_COMMAND);
		if ((fp = popen(xauth_command, "w")) == NULL)
			goto free_xauth_env;
		fputs(xauth, fp);
		pclose(fp);
		free(xauth_command);
	}
#if 1	/*
	 * To make bash execute ~/.bashrc, because bash only reads it, if
	 *   1. $SSH_CLIENT/$SSH2_CLIENT is set, or stdin is a socket.
	 * and
	 *   2. $SHLVL < 2
	 * Honestly, people should use zsh instead of bash.
	 */
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, stdin_pipe) == -1)
#else
	if (pipe(stdin_pipe) == -1)
#endif
	{
		e = gfarm_errno_to_error(errno);
		goto free_xauth_env;
	}
	if (pipe(stdout_pipe) == -1) {
		e = gfarm_errno_to_error(errno);
		goto close_stdin_pipe;
	}
	if (pipe(stderr_pipe) == -1) {
		e = gfarm_errno_to_error(errno);
		goto close_stdout_pipe;
	}
	cc->pid = cc->exited_pid = 0;
	if ((cc->pid = fork()) == 0) {
		struct rlimit rlim;

		/*
		 * XXX - Some Linux distributions set coredump size 0
		 *	 by default.
		 */
		if (getrlimit(RLIMIT_CORE, &rlim) != -1) {
			rlim.rlim_cur = rlim.rlim_max;
			setrlimit(RLIMIT_CORE, &rlim);
		}

		/* child */
		dup2(stdin_pipe[0], 0);
		dup2(stdout_pipe[1], 1);
		dup2(stderr_pipe[1], 2);
		close(stderr_pipe[1]);
		close(stderr_pipe[0]);
		close(stdout_pipe[1]);
		close(stdout_pipe[0]);
		close(stdin_pipe[1]);
		close(stdin_pipe[0]);
		/* close client connection, syslog and other sockets */
		for (i = 3; i < stderr_pipe[1]; i++)
			close(i);
		/* re-install default signal handler (see main) */
		signal(SIGPIPE, SIG_DFL);
		/*
		 * create a process group
		 * to make it possible to send a signal later
		 */
		setpgid(0, getpid());
		umask(command_umask);
		execve(command, argv_storage, envp);
		fprintf(stderr, "%s: ", gfarm_host_get_self_name());
		perror(path);
		_exit(1);
	} else if (cc->pid == -1) {
		e = gfarm_errno_to_error(errno);
		goto close_stderr_pipe;
	}
	close(stderr_pipe[1]);
	close(stdout_pipe[1]);
	close(stdin_pipe[0]);
	error = GFARM_ERR_NO_ERROR;
	goto rpc_reply;

close_stderr_pipe:
	close(stderr_pipe[0]);
	close(stderr_pipe[1]);
close_stdout_pipe:
	close(stdout_pipe[0]);
	close(stdout_pipe[1]);
close_stdin_pipe:
	close(stdin_pipe[0]);
	close(stdin_pipe[1]);
free_xauth_env:
	if (use_xauth_env)
		free(xauth_env);
remove_xauth:
	if (use_xauth_env)
		unlink(xauth_filename);
free_xauth:
	if (use_xauth_env)
		free(xauth);
free_envp_array:
	for (i = 0; i < nenv; i++)
		free(envp[i]);
free_argv_array:
	for (i = 0; i < argc; i++)
		free(argv[i]);
free_shell_env:
	free(shell_env);
free_home_env:
	free(home_env);
free_user_env:
	free(user_env);
free_envp:
	free(envp);
free_argv:
	free(argv_storage);
rpc_error:
	free(path);
	error = e;
rpc_reply:
	gfs_server_put_reply(client, "command-start", error, "i", cc->pid);
	gfp_xdr_flush(client);
	if (error != GFARM_ERR_NO_ERROR)
		return;

	/*
	 * Now, we set the connection file descriptor non-blocking mode.
	 */
	if (fcntl(conn_fd, F_SETFL, O_NONBLOCK) == -1) /* shouldn't fail */
		gflog_warning("command-start:nonblock: %s", strerror(errno));

	siz = sizeof(i);
	if (getsockopt(conn_fd, SOL_SOCKET, SO_RCVBUF, &i, &siz))
		i = GFARM_DEFAULT_COMMAND_IOBUF_SIZE;
	cc->iobuffer[FDESC_STDIN] = gfarm_iobuffer_alloc(i);

	siz = sizeof(i);
	if (getsockopt(conn_fd, SOL_SOCKET, SO_SNDBUF, &i, &siz))
		i = GFARM_DEFAULT_COMMAND_IOBUF_SIZE;
	cc->iobuffer[FDESC_STDOUT] = gfarm_iobuffer_alloc(i);
	cc->iobuffer[FDESC_STDERR] = gfarm_iobuffer_alloc(i);

	gfarm_iobuffer_set_nonblocking_read_xxx(
		cc->iobuffer[FDESC_STDIN], client);
	gfarm_iobuffer_set_nonblocking_write_fd(
		cc->iobuffer[FDESC_STDIN], stdin_pipe[1]);

	gfarm_iobuffer_set_nonblocking_read_fd(
		cc->iobuffer[FDESC_STDOUT], stdout_pipe[0]);
	gfarm_iobuffer_set_nonblocking_write_xxx(
		cc->iobuffer[FDESC_STDOUT], client);

	gfarm_iobuffer_set_nonblocking_read_fd(
		cc->iobuffer[FDESC_STDERR], stderr_pipe[0]);
	gfarm_iobuffer_set_nonblocking_write_xxx(
		cc->iobuffer[FDESC_STDERR], client);

	while (cc->server_state != GFS_COMMAND_SERVER_STATE_EXITED)
		gfs_server_command_io(client, NULL);

	gfs_server_client_command_result(client);

	/*
	 * clean up
	 */

	gfarm_iobuffer_free(cc->iobuffer[FDESC_STDIN]);
	gfarm_iobuffer_free(cc->iobuffer[FDESC_STDOUT]);
	gfarm_iobuffer_free(cc->iobuffer[FDESC_STDERR]);

	close(stderr_pipe[0]);
	close(stdout_pipe[0]);
	close(stdin_pipe[1]);
	if (use_xauth_env) {
		free(xauth_env);
		unlink(xauth_filename);
		free(xauth);
	}
	for (i = 0; i < nenv; i++)
		free(envp[i]);
	for (i = 0; i < argc; i++)
		free(argv[i]);
	free(envp);
	free(argv_storage);
	free(path);
}
#endif /* not yet in gfarm v2 */

void
server(int client_fd, char *client_name, struct sockaddr *client_addr)
{
	gfarm_error_t e;
	struct gfp_xdr *client;
	int eof;
	gfarm_int32_t request;
	char *aux, addr_string[GFARM_SOCKADDR_STRLEN];

	e = gfm_client_connection_acquire(gfarm_metadb_server_name,
	    gfarm_metadb_server_port, &gfm_server);
	if (e != GFARM_ERR_NO_ERROR)
		fatal("connecting to gfmd: %s", gfarm_error_string(e));
	gfarm_metadb_set_server(gfm_server);

	if (client_name == NULL) { /* i.e. not UNIX domain socket case */
		char *s;

		e = gfarm_sockaddr_to_name(client_addr, &client_name);
		if (e != GFARM_ERR_NO_ERROR) {
			gfarm_sockaddr_to_string(client_addr,
			    addr_string, GFARM_SOCKADDR_STRLEN);
			gflog_warning("%s: %s", addr_string,
			    gfarm_error_string(e));
			client_name = strdup(addr_string);
			if (client_name == NULL)
				fatal("%s: no memory", addr_string);
		}
		e = gfarm_host_get_canonical_name(client_name, &s);
		if (e == GFARM_ERR_NO_ERROR) {
			free(client_name);
			client_name = s;
		}
	}

#if 0 /* not yet in gfarm v2 */
	e = gfarm_netparam_config_get_long(&gfarm_netparam_file_read_size,
	    client_name, client_addr, &file_read_size);
	if (e != GFARM_ERR_NO_ERROR) /* shouldn't happen */
		fatal("file_read_size: %s", gfarm_error_string(e));

	e = gfarm_netparam_config_get_long(&gfarm_netparam_rate_limit,
	    client_name, client_addr, &rate_limit);
	if (e != GFARM_ERR_NO_ERROR) /* shouldn't happen */
		fatal("rate_limit: %s", gfarm_error_string(e));
#endif /* not yet in gfarm v2 */

	e = gfp_xdr_new_fd(client_fd, &client);
	if (e != GFARM_ERR_NO_ERROR) {
		close(client_fd);
		fatal("%s: gfp_xdr_new: %s",
		    client_name, gfarm_error_string(e));
	}
	/*
	 * The following function switches deamon's privilege
	 * to the authenticated user.
	 * This also enables gfarm_get_global_username() and
	 * gfarm_get_local_homedir() which are necessary for
	 * gfs_client_connect() called from gfs_server_replicate_file().
	 */
	seteuid(0);
	e = gfarm_authorize(client, 0, GFS_SERVICE_TAG,
	    client_name, client_addr, NULL, &username, NULL);
	seteuid(gfsd_uid);
	if (e != GFARM_ERR_NO_ERROR)
		fatal("%s: gfarm_authorize: %s",
		    client_name, gfarm_error_string(e));
	aux = malloc(strlen(username) + 1 + strlen(client_name) + 1);
	if (aux == NULL)
		fatal("%s: no memory\n", client_name);
	sprintf(aux, "%s@%s", username, client_name);
	gflog_set_auxiliary_info(aux);	

	/* set file creation mask */
	command_umask = umask(0);

	for (;;) {
		e = gfp_xdr_recv(client, 0, &eof, "i", &request);
		if (e != GFARM_ERR_NO_ERROR)
			fatal("request number: %s", gfarm_error_string(e));
		if (eof) {
			cleanup(0);
			exit(0);
		}
		switch (request) {
		case GFS_PROTO_PROCESS_SET:
			gfs_server_process_set(client); break;
		case GFS_PROTO_OPEN_LOCAL:
			gfs_server_open_local(client); break;
		case GFS_PROTO_OPEN:	gfs_server_open(client); break;
		case GFS_PROTO_CLOSE:	gfs_server_close(client); break;
		case GFS_PROTO_PREAD:	gfs_server_pread(client); break;
		case GFS_PROTO_PWRITE:	gfs_server_pwrite(client); break;
		case GFS_PROTO_FTRUNCATE: gfs_server_ftruncate(client); break;
		case GFS_PROTO_FSYNC:	gfs_server_fsync(client); break;
		case GFS_PROTO_FSTAT:	gfs_server_fstat(client); break;
		case GFS_PROTO_CKSUM_SET: gfs_server_cksum_set(client); break;
#if 0 /* not yet in gfarm v2 */
		case GFS_PROTO_STATFS:	gfs_server_statfs(client); break;
		case GFS_PROTO_COMMAND:
			if (credential_exported == NULL) {
				e = gfp_xdr_export_credential(client);
				if (e == GFARM_ERR_NO_ERROR)
					credential_exported = client;
				else
					gflog_warning(
					    "export delegated credential: %s",
					    gfarm_error_string(e));
			}
			gfs_server_command(client,
			    credential_exported == NULL ? NULL :
			    gfp_xdr_env_for_credential(client));
			break;
#endif /* not yet in gfarm v2 */
		default:
			gflog_warning("unknown request %d", (int)request);
			cleanup(0);
			exit(1);
		}
	}
}

void
datagram_server(int sock)
{
	int rv;
	struct sockaddr_in client_addr;
	socklen_t client_addr_size = sizeof(client_addr);
	double loadavg[3];
#ifndef WORDS_BIGENDIAN
	struct { char c[8]; } nloadavg[3];
#else
#	define nloadavg loadavg
#endif
	char buffer[1024];

	rv = recvfrom(sock, buffer, sizeof(buffer), 0,
	    (struct sockaddr *)&client_addr, &client_addr_size);
	if (rv == -1)
		return;
	rv = getloadavg(loadavg, GFARM_ARRAY_LENGTH(loadavg));
	if (rv == -1) {
		gflog_warning("datagram_server: cannot get load average");
		return;
	}
#ifndef WORDS_BIGENDIAN
	swab(&loadavg[0], &nloadavg[0], sizeof(nloadavg[0]));
	swab(&loadavg[1], &nloadavg[1], sizeof(nloadavg[1]));
	swab(&loadavg[2], &nloadavg[2], sizeof(nloadavg[2]));
#endif
	rv = sendto(sock, nloadavg, sizeof(nloadavg), 0,
	    (struct sockaddr *)&client_addr, sizeof(client_addr));
}

int
open_accepting_inet_socket(int port)
{
	gfarm_error_t e;
	struct sockaddr_in self_addr;
	socklen_t self_addr_size;
	int sock, sockopt;

	memset(&self_addr, 0, sizeof(self_addr));
	self_addr.sin_family = AF_INET;
	self_addr.sin_addr.s_addr = INADDR_ANY;
	self_addr.sin_port = htons(port);
	self_addr_size = sizeof(self_addr);
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		gflog_fatal_errno("accepting socket");
	sockopt = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
	    &sockopt, sizeof(sockopt)) == -1)
		gflog_warning_errno("SO_REUSEADDR");
	if (bind(sock, (struct sockaddr *)&self_addr, self_addr_size) < 0)
		gflog_fatal_errno("bind accepting socket");
	e = gfarm_sockopt_apply_listener(sock);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning("setsockopt: %s", gfarm_error_string(e));
	if (listen(sock, LISTEN_BACKLOG) < 0)
		gflog_fatal_errno("listen");
	return (sock);
}

int
open_accepting_local_socket(int port)
{
	socklen_t local_sockname_size;
	int sock, save_errno;
	struct stat st;

	memset(&local_sockname, 0, sizeof(local_sockname));
	local_sockname.sun_family = AF_UNIX;
	local_sockname_size = snprintf(
	    local_sockname.sun_path, sizeof local_sockname.sun_path,
	    GFSD_LOCAL_SOCKET_NAME, port);
#ifdef SUN_LEN /* derived from 4.4BSD */
	local_sockname_size = SUN_LEN(&local_sockname);
#else
	local_sockname_size +=
	    sizeof(local_sockname) - sizeof(local_sockname.sun_path);
#endif

	snprintf(local_sockdir, sizeof local_sockdir,
	    GFSD_LOCAL_SOCKET_DIR, port);
	if (mkdir(local_sockdir, LOCAL_SOCKDIR_MODE) == -1) {
		if (errno != EEXIST) {
			gflog_fatal("%s: cannot mkdir: %s",
			    local_sockdir, strerror(errno));
		} else if (stat(local_sockdir, &st) != 0) {
			gflog_fatal("stat(%s): %s",
			    local_sockdir, strerror(errno));
		} else if (st.st_uid != gfsd_uid) {
			gflog_fatal("%s: not owned by uid %d",
			    local_sockdir, gfsd_uid);
		} else if ((st.st_mode & PERMISSION_MASK) !=
		    LOCAL_SOCKDIR_MODE &&
		    chmod(local_sockdir, LOCAL_SOCKDIR_MODE) != 0) {
			gflog_fatal("%s: cannot chmod to 0%o: %s",
			    local_sockdir, LOCAL_SOCKDIR_MODE,
			    strerror(errno));
		}
	}

	sock = socket(PF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		save_errno = errno;
		rmdir(local_sockdir);
		gflog_fatal("creating UNIX domain socket: %s",
		    strerror(save_errno));
	}
	unlink(local_sockname.sun_path);
	if (bind(sock, (struct sockaddr *)&local_sockname,
	    local_sockname_size) < 0) {
		save_errno = errno;
		rmdir(local_sockdir);
		gflog_fatal("%s: cannot bind UNIX domain socket: %s",
		    local_sockname.sun_path, strerror(save_errno));
	}
	if (listen(sock, LISTEN_BACKLOG) < 0) {
		save_errno = errno;
		unlink(local_sockname.sun_path);
		rmdir(local_sockdir);
		gflog_fatal("listen UNIX domain socket: %s",
		    strerror(save_errno));
	}

	/* Linux at least since 2.4 needs this. */
	chmod(local_sockname.sun_path, LOCAL_SOCKET_MODE);

	return (sock);
}

void
open_datagram_service_sockets(int port, int *countp, int **socketsp)
{
	gfarm_error_t e;
	int i, self_addresses_count, *sockets, s;
	struct in_addr *self_addresses;
	struct sockaddr_in bind_addr;
	socklen_t bind_addr_size;

	e = gfarm_get_ip_addresses(&self_addresses_count, &self_addresses);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal("get_ip_addresses: %s", gfarm_error_string(e));
	sockets = malloc(sizeof(*sockets) * self_addresses_count);
	if (sockets == NULL)
		gflog_fatal_errno("malloc datagram sockets");
	for (i = 0; i < self_addresses_count; i++) {
		memset(&bind_addr, 0, sizeof(bind_addr));
		bind_addr.sin_family = AF_INET;
		bind_addr.sin_addr = self_addresses[i];
		bind_addr.sin_port = ntohs(port);
		bind_addr_size = sizeof(bind_addr);
		s = socket(PF_INET, SOCK_DGRAM, 0);
		if (s < 0)
			gflog_fatal_errno("datagram socket");
		if (bind(s, (struct sockaddr *)&bind_addr, bind_addr_size) < 0)
			gflog_fatal_errno("datagram bind");
		sockets[i] = s;
	}
	*countp = self_addresses_count;
	*socketsp = sockets;
	free(self_addresses);
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [option]\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-P <pid-file>\n");
	fprintf(stderr, "\t-f <gfarm-configuration-file>\n");
	fprintf(stderr, "\t-p <port>\n");
	fprintf(stderr, "\t-s <syslog-facility>\n");
	fprintf(stderr, "\t-v>\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	extern char *optarg;
	extern int optind;
	struct sockaddr_in client_addr, self_addr;
	struct sockaddr_un client_local_addr;
	socklen_t client_addr_size;
	gfarm_error_t e;
	char *config_file = NULL, *port_number = NULL, *pid_file = NULL;
	char *canonical_self_name;
	struct passwd *gfsd_pw;
	FILE *pid_fp = NULL;
	int syslog_facility = GFARM_DEFAULT_FACILITY;
	int ch, table_size, datagram_socks_count, i, nfound;
	int accepting_inet_sock, accepting_local_sock, *datagram_socks;
	int client, max_fd;
	struct sigaction sa;
	fd_set requests;

	if (argc >= 1)
		program_name = basename(argv[0]);
	gflog_set_identifier(program_name);

	while ((ch = getopt(argc, argv, "P:df:p:s:uv")) != -1) {
		switch (ch) {
		case 'P':
			pid_file = optarg;
			break;
		case 'd':
			debug_mode = 1;
			break;
		case 'f':
			config_file = optarg;
			break;
		case 'p':
			port_number = optarg;
			break;
		case 's':
			syslog_facility =
			    gflog_syslog_name_to_facility(optarg);
			if (syslog_facility == -1)
				gflog_fatal("%s: unknown syslog facility",
				    optarg);
			break;
		case 'u':
			restrict_user = 1;
			restricted_user = getuid();
			break;
		case 'v':
			gflog_auth_set_verbose(1);
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (config_file != NULL)
		gfarm_config_set_filename(config_file);
	e = gfarm_server_initialize();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_server_initialize: %s\n",
		    gfarm_error_string(e));
		exit(1);
	}
	if (port_number != NULL)
		gfarm_spool_server_port = strtol(port_number, NULL, 0);

	gfsd_pw = getpwnam(GFSD_USERNAME);
	if (gfsd_pw == NULL) {
		fprintf(stderr, "user `%s' is necessary, but doesn't exist.\n",
		    GFSD_USERNAME);
		exit(1);
	}
	gfsd_uid = gfsd_pw->pw_uid;
	seteuid(gfsd_uid);

	e = gfarm_set_local_user_for_this_local_account();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "acquiring information about user `%s': %s\n",
		    GFSD_USERNAME, gfarm_error_string(e));
		exit(1);
	}
	e = gfarm_set_global_username(GFSD_USERNAME);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, ": gfarm_set_global_username: %s\n",
		    gfarm_error_string(e));
		exit(1);
	}

	/* XXX FIXME - if failed, should report it to syslog, and retry */
	e = gfm_client_connection_acquire(gfarm_metadb_server_name,
	    gfarm_metadb_server_port, &gfm_server);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "connecting gfmd: %s\n",
		    gfarm_error_string(e));
		exit(1);
	}
	gfarm_metadb_set_server(gfm_server);
	e = gfarm_host_get_canonical_self_name(&canonical_self_name);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr,
		    "cannot get canonical hostname of this node: %s\n",
		    gfarm_error_string(e));
		exit(1);
	}
	e = gfarm_host_address_get(canonical_self_name, 0,
	    (struct sockaddr *)&self_addr, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr,
		    "cannot get IP address of `%s': %s\n",
		    canonical_self_name, gfarm_error_string(e));
		exit(1);
	}

	seteuid(0);

	/* XXX - kluge for gfrcmd (to mkdir HOME....) for now */
	if (chdir(gfarm_spool_root) == -1)
		gflog_fatal_errno(gfarm_spool_root);

	open_datagram_service_sockets(gfarm_spool_server_port,
	    &datagram_socks_count, &datagram_socks);
	accepting_inet_sock =
	    open_accepting_inet_socket(gfarm_spool_server_port);
	accepting_local_sock =
	    open_accepting_local_socket(gfarm_spool_server_port);

	seteuid(gfsd_uid);

	max_fd = accepting_inet_sock > accepting_local_sock ?
	    accepting_inet_sock : accepting_local_sock;
	for (i = 0; i < datagram_socks_count; i++) {
		if (max_fd < datagram_socks[i])
			max_fd = datagram_socks[i];
	}
	if (max_fd > FD_SETSIZE)
		gflog_fatal("datagram_service: too big file descriptor");

	if (pid_file != NULL) {
		/*
		 * We do this before calling gfarm_daemon()
		 * to print the error message to stderr.
		 */
		seteuid(0);
		pid_fp = fopen(pid_file, "w");
		seteuid(gfsd_uid);
		if (pid_fp == NULL)
			gflog_fatal_errno(pid_file);
	}
	if (!debug_mode) {
		gflog_syslog_open(LOG_PID, syslog_facility);
		gfarm_daemon(0, 0);
	}

	/* We do this after calling gfarm_daemon(), because it changes pid. */
	master_gfsd_pid = getpid();
	sa.sa_handler = cleanup_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGHUP, &sa, NULL); /* XXX - need to restart gfsd */
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	if (pid_file != NULL) {
		fprintf(pid_fp, "%ld\n", (long)master_gfsd_pid);
		fclose(pid_fp);
	}

	table_size = FILE_TABLE_LIMIT;
	gfarm_unlimit_nofiles(&table_size);
	if (table_size > FILE_TABLE_LIMIT)
		table_size = FILE_TABLE_LIMIT;
	file_table_init(table_size);
	OpenSSL_add_all_digests(); /* for EVP_get_digestbyname() */

#if 0 /* not yet in gfarm v2 */
	/*
	 * Because SA_NOCLDWAIT is not implemented on some OS,
	 * we do not rely on the feature.
	 */
	sa.sa_handler = sigchld_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sa, NULL);
#endif

	/*
	 * We don't want SIGPIPE, but want EPIPE on write(2)/close(2).
	 */
	signal(SIGPIPE, SIG_IGN);

	for (;;) {
		FD_ZERO(&requests);
		FD_SET(accepting_inet_sock, &requests);
		FD_SET(accepting_local_sock, &requests);
		for (i = 0; i < datagram_socks_count; i++)
			FD_SET(datagram_socks[i], &requests);
		nfound = select(max_fd + 1, &requests, NULL, NULL, NULL);
		if (nfound <= 0) {
			if (nfound == 0 || errno == EINTR || errno == EAGAIN)
				continue;
			fatal_errno("select");
		}

		if (FD_ISSET(accepting_inet_sock, &requests)) {
			client_addr_size = sizeof(client_addr);
			client = accept(accepting_inet_sock,
			   (struct sockaddr *)&client_addr, &client_addr_size);
			if (client < 0) {
				if (errno == EINTR)
					continue;
				fatal_errno("accept");
			}
#ifndef GFSD_DEBUG
			switch (fork()) {
			case 0:
#endif
				close(accepting_inet_sock);
				close(accepting_local_sock);
				for (i = 0; i < datagram_socks_count; i++)
					close(datagram_socks[i]);
				gfm_client_connection_free(gfm_server);
				gfm_server = NULL;

				server(client, NULL,
				    (struct sockaddr *)&client_addr);
				/*NOTREACHED*/
#ifndef GFSD_DEBUG
			case -1:
				gflog_warning_errno("fork");
				/*FALLTHROUGH*/
			default:
				close(client);
				break;
			}
#endif
		}
		if (FD_ISSET(accepting_local_sock, &requests)) {
			client_addr_size = sizeof(client_local_addr);
			client = accept(accepting_inet_sock,
			   (struct sockaddr *)&client_local_addr,
			    &client_addr_size);
			if (client < 0) {
				if (errno == EINTR)
					continue;
				fatal_errno("accept local socket");
			}
#ifndef GFSD_DEBUG
			switch (fork()) {
			case 0:
#endif
				close(accepting_inet_sock);
				close(accepting_local_sock);
				for (i = 0; i < datagram_socks_count; i++)
					close(datagram_socks[i]);
				gfm_client_connection_free(gfm_server);
				gfm_server = NULL;

				/* NOTE: we pass IPv4 address here */
				server(client, canonical_self_name,
				    (struct sockaddr *)&self_addr);
				/*NOTREACHED*/
#ifndef GFSD_DEBUG
			case -1:
				gflog_warning_errno("fork");
				/*FALLTHROUGH*/
			default:
				close(client);
				break;
			}
#endif
		}
		for (i = 0; i < datagram_socks_count; i++) {
			if (FD_ISSET(datagram_socks[i], &requests))
				datagram_server(datagram_socks[i]);
		}
	}
	/*NOTREACHED*/
#ifdef __GNUC__ /* to shut up warning */
	return (0);
#endif
}
