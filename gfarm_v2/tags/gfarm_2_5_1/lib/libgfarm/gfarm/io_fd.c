/*
 * iobuffer operation: file descriptor read/write
 */

#include <gfarm/gfarm_config.h>

#include <sys/types.h>
#ifdef HAVE_POLL
#include <poll.h>
#else
#include <sys/time.h>
#endif
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h> /* for definition of gfarm_off_t */

#include "gfutil.h" /* gfarm_send_no_sigpipe() */

#include "iobuffer.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "config.h"

/*
 * nonblocking i/o
 */
int
gfarm_iobuffer_nonblocking_read_fd_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	ssize_t rv = read(fd, data, length);

	if (rv == -1)
		gfarm_iobuffer_set_error(b, gfarm_errno_to_error(errno));
	return (rv);

}

static int
gfarm_iobuffer_nonblocking_write_fd_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	ssize_t rv = write(fd, data, length);

	if (rv == -1)
		gfarm_iobuffer_set_error(b, gfarm_errno_to_error(errno));
	return (rv);
}

/*
 * We have to distinguish the write operation for sockets from
 * the operation for file descriptors, because gfarm_send_no_sigpipe()
 * may only work with sockets, since it may use send(2) internally.
 */
int
gfarm_iobuffer_nonblocking_write_socket_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	ssize_t rv = gfarm_send_no_sigpipe(fd, data, length);

	if (rv == -1)
		gfarm_iobuffer_set_error(b, gfarm_errno_to_error(errno));
	return (rv);
}

void
gfarm_iobuffer_set_nonblocking_read_fd(struct gfarm_iobuffer *b, int fd)
{
	gfarm_iobuffer_set_read_timeout(b, 
	    gfarm_iobuffer_nonblocking_read_fd_op, NULL, fd);
}

void
gfarm_iobuffer_set_nonblocking_write_fd(struct gfarm_iobuffer *b, int fd)
{
	gfarm_iobuffer_set_write(b, gfarm_iobuffer_nonblocking_write_fd_op,
	    NULL, fd);
}

/*
 * blocking i/o
 */
int
gfarm_iobuffer_blocking_read_timeout_fd_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	ssize_t rv;
	int avail;

	for (;;) {
#ifdef HAVE_POLL
		struct pollfd fds[1];

		fds[0].fd = fd;
		fds[0].events = POLLIN;
		avail = poll(fds, 1, gfarm_network_receive_timeout * 1000);
#else
		fd_set readable;
		struct timeval tv;

		FD_ZERO(&readable);
		FD_SET(fd, &readable);
		tv.tv_sec = gfarm_network_receive_timeout;
		tv.tv_usec = 0;
		avail = select(fd + 1, &readable, NULL, NULL, &tv);
#endif
		if (avail == 0) {
			gfarm_iobuffer_set_error(b,
			    GFARM_ERR_OPERATION_TIMED_OUT);
			return (-1);
		} else if (avail == -1) {
			if (errno == EINTR)
				continue;
			gfarm_iobuffer_set_error(b, 
			    gfarm_errno_to_error(errno));
			return (-1);
		}

		/*
		 * On Linux, read() below sometimes fails and 'errno'
		 * is set to EAGAIN though we have confirmed the file
		 * descriptor is ready to read by calling poll() or
		 * select().  We should retry poll() or select() in
		 * that case.
		 */
		rv = read(fd, data, length);
		if (rv == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			gfarm_iobuffer_set_error(b,
			    gfarm_errno_to_error(errno));
			return (-1);
		}
		return (rv);
	}
}

int
gfarm_iobuffer_blocking_read_notimeout_fd_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	ssize_t rv;

	for (;;) {
		rv = read(fd, data, length);
		if (rv == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN) {
#ifdef HAVE_POLL
				struct pollfd fds[1];

				fds[0].fd = fd;
				fds[0].events = POLLIN;
				poll(fds, 1, -1);
#else
				fd_set readable;

				FD_ZERO(&readable);
				FD_SET(fd, &readable);
				select(fd + 1, &readable, NULL, NULL, NULL);
#endif
				continue;
			}
			gfarm_iobuffer_set_error(b,
			    gfarm_errno_to_error(errno));
		}
		return (rv);
	}
}

int
gfarm_iobuffer_blocking_write_socket_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	ssize_t rv;

	for (;;) {
		rv = gfarm_send_no_sigpipe(fd, data, length);
		if (rv == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN) {
#ifdef HAVE_POLL
				struct pollfd fds[1];

				fds[0].fd = fd;
				fds[0].events = POLLOUT;
				fds[0].revents = 0;
				poll(fds, 1, -1);
#else
				fd_set writable;

				FD_ZERO(&writable);
				FD_SET(fd, &writable);
				select(fd + 1, NULL, &writable, NULL, NULL);
#endif
				continue;
			}
			gfarm_iobuffer_set_error(b,
			    gfarm_errno_to_error(errno));
		}
		return (rv);
	}
}

/*
 * an option for gfarm_iobuffer_set_write_close()
 */
#if 0 /* currently not used */
void
gfarm_iobuffer_write_close_fd_op(struct gfarm_iobuffer *b,
	void *cookie, int fd)
{
	int rv = close(fd);

	if (rv == -1 && gfarm_iobuffer_get_error(b) == GFARM_ERR_NO_ERROR)
		gfarm_iobuffer_set_error(b, gfarm_errno_to_error(errno));
}
#endif /* currently not used */

/*
 * gfp_xdr operation
 */

gfarm_error_t
gfp_iobuffer_close_fd_op(void *cookie, int fd)
{
	return (close(fd) == -1 ? gfarm_errno_to_error(errno) :
	    GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_iobuffer_export_credential_fd_op(void *cookie)
{
	/* it's already exported, or no way to export it. */
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_iobuffer_delete_credential_fd_op(void *cookie, int sighandler)
{
	return (GFARM_ERR_NO_ERROR);
}

char *
gfp_iobuffer_env_for_credential_fd_op(void *cookie)
{
	return (NULL);
}

struct gfp_iobuffer_ops gfp_xdr_socket_iobuffer_ops = {
	gfp_iobuffer_close_fd_op,
	gfp_iobuffer_export_credential_fd_op,
	gfp_iobuffer_delete_credential_fd_op,
	gfp_iobuffer_env_for_credential_fd_op,
	gfarm_iobuffer_nonblocking_read_fd_op,
	gfarm_iobuffer_nonblocking_write_socket_op,
	gfarm_iobuffer_blocking_read_timeout_fd_op,
	gfarm_iobuffer_blocking_read_notimeout_fd_op,
	gfarm_iobuffer_blocking_write_socket_op
};

gfarm_error_t
gfp_xdr_new_socket(int fd, struct gfp_xdr **connp)
{
	return (gfp_xdr_new(&gfp_xdr_socket_iobuffer_ops, NULL, fd, connp));
}

gfarm_error_t
gfp_xdr_set_socket(struct gfp_xdr *conn, int fd)
{
	gfp_xdr_set(conn, &gfp_xdr_socket_iobuffer_ops, NULL, fd);
	return (GFARM_ERR_NO_ERROR);
}
