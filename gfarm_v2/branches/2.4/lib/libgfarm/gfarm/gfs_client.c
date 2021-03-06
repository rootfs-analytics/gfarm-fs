#define _POSIX_PII_SOCKET /* to use struct msghdr on Tru64 */
#include <assert.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h> /* sprintf() */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include <openssl/evp.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#if defined(SCM_RIGHTS) && \
		(!defined(sun) || (!defined(__svr4__) && !defined(__SVR4)))
#define HAVE_MSG_CONTROL 1
#endif

#include "gfutil.h"
#include "gfevent.h"
#include "hash.h"
#include "lru_cache.h"

#include "liberror.h"
#include "sockutil.h"
#include "iobuffer.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "host.h"
#include "param.h"
#include "sockopt.h"
#include "auth.h"
#include "config.h"
#include "conn_cache.h"
#include "gfs_proto.h"
#include "gfs_client.h"
#include "gfm_client.h"

#define GFS_CLIENT_CONNECT_TIMEOUT	30 /* seconds */
#define GFS_CLIENT_COMMAND_TIMEOUT	20 /* seconds */

#define XAUTH_NEXTRACT_MAXLEN	512

struct gfs_connection {
	struct gfp_cached_connection *cache_entry;

	struct gfp_xdr *conn;
	char *hostname;
	int port;
	enum gfarm_auth_method auth_method;

	int is_local;
	gfarm_pid_t pid; /* parallel process ID */

	int opened; /* reference counter */

	void *context; /* work area for RPC (esp. GFS_PROTO_COMMAND) */
};

#define SERVER_HASHTAB_SIZE	3079	/* prime number */

static gfarm_error_t gfs_client_connection_dispose(void *);

static struct gfp_conn_cache gfs_server_cache =
	GFP_CONN_CACHE_INITIALIZER(gfs_server_cache,
		gfs_client_connection_dispose,
		"gfs_connection",
		SERVER_HASHTAB_SIZE,
		&gfarm_gfsd_connection_cache);

static gfarm_error_t
	(*gfs_client_hook_for_connection_error)(struct gfs_connection *) =
		NULL;

/*
 * Currently this supports only one hook function.
 * And the function is always gfarm_schedule_host_cache_clear_auth() (or NULL).
 */
void
gfs_client_add_hook_for_connection_error(
	gfarm_error_t (*hook)(struct gfs_connection *))
{
	gfs_client_hook_for_connection_error = hook;
}

static void
gfs_client_execute_hook_for_connection_error(struct gfs_connection *gfs_server)
{
	if (gfs_client_hook_for_connection_error != NULL)
		(*gfs_client_hook_for_connection_error)(gfs_server);
}

int
gfs_client_is_connection_error(gfarm_error_t e)
{
	return (IS_CONNECTION_ERROR(e));
}

int
gfs_client_connection_fd(struct gfs_connection *gfs_server)
{
	return (gfp_xdr_fd(gfs_server->conn));
}

enum gfarm_auth_method
gfs_client_connection_auth_method(struct gfs_connection *gfs_server)
{
	return (gfs_server->auth_method);
}

const char *
gfs_client_hostname(struct gfs_connection *gfs_server)
{
	return (gfs_server->hostname);
}

const char *
gfs_client_username(struct gfs_connection *gfs_server)
{
	return (gfp_cached_connection_username(gfs_server->cache_entry));
}

int
gfs_client_port(struct gfs_connection *gfs_server)
{
	return (gfs_server->port);
}

int
gfs_client_connection_is_local(struct gfs_connection *gfs_server)
{
	return (gfs_server->is_local);
}

gfarm_pid_t
gfs_client_pid(struct gfs_connection *gfs_server)
{
	return (gfs_server->pid);
}

#define gfs_client_connection_is_cached(gfs_server) \
	gfp_is_cached_connection((gfs_server)->cache_entry)

#define gfs_client_connection_used(gfs_server) \
	gfp_cached_connection_used(&gfs_server_cache, \
	    (gfs_server)->cache_entry)

void
gfs_client_purge_from_cache(struct gfs_connection *gfs_server)
{
	gfp_cached_connection_purge_from_cache(&gfs_server_cache,
	    gfs_server->cache_entry);
}

void
gfs_client_connection_gc(void)
{
	gfp_cached_connection_gc_all(&gfs_server_cache);
}

static int
sockaddr_is_local(struct sockaddr *peer_addr)
{
	static int self_ip_asked = 0;
	static int self_ip_count = 0;
	static struct in_addr *self_ip_list;

	struct sockaddr_in *peer_in;
	int i;

	if (!self_ip_asked) {
		self_ip_asked = 1;
		if (gfarm_get_ip_addresses(&self_ip_count, &self_ip_list) !=
		    GFARM_ERR_NO_ERROR) {
			/* self_ip_count remains 0 */
			return (0);
		}
	}
	if (peer_addr->sa_family != AF_INET)
		return (0);
	peer_in = (struct sockaddr_in *)peer_addr;
	/* XXX if there are lots of IP address on this host, this is slow */
	for (i = 0; i < self_ip_count; i++) {
		if (peer_in->sin_addr.s_addr == self_ip_list[i].s_addr)
			return (1);
	}
	return (0);
}

static gfarm_error_t
gfs_client_connect_unix(struct sockaddr *peer_addr, int *sockp)
{
	int rv, sock, save_errno;
	struct sockaddr_un peer_un;
	struct sockaddr_in *peer_in;
	socklen_t socklen;

	assert(peer_addr->sa_family == AF_INET);
	peer_in = (struct sockaddr_in *)peer_addr;

	sock = socket(PF_UNIX, SOCK_STREAM, 0);
	if (sock == -1 && (errno == ENFILE || errno == EMFILE)) {
		gfs_client_connection_gc(); /* XXX FIXME: GC all descriptors */
		sock = socket(PF_UNIX, SOCK_STREAM, 0);
	}
	if (sock == -1) {
		save_errno = errno;
		gflog_debug(GFARM_MSG_1001172,
			"creation of UNIX socket failed: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}

	memset(&peer_un, 0, sizeof(peer_un));
	/* XXX inet_ntoa() is not MT-safe on some platforms */
	socklen = snprintf(peer_un.sun_path, sizeof(peer_un.sun_path),
	    GFSD_LOCAL_SOCKET_NAME, inet_ntoa(peer_in->sin_addr),
	    ntohs(peer_in->sin_port));
	peer_un.sun_family = AF_UNIX;
#ifdef SUN_LEN /* derived from 4.4BSD */
	socklen = SUN_LEN(&peer_un);
#else
	socklen += sizeof(peer_un) - sizeof(peer_un.sun_path);
#endif
	rv = connect(sock, (struct sockaddr *)&peer_un, socklen);
	if (rv == -1) {
		save_errno = errno;
		close(sock);
		if (save_errno != ENOENT) {
			gflog_debug(GFARM_MSG_1001173,
				"connect() with UNIX socket failed: %s",
				strerror(save_errno));
			return (gfarm_errno_to_error(save_errno));
		}

		/* older gfsd doesn't support UNIX connection, try INET */
		sock = -1;
	}
	*sockp = sock;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_client_connect_inet(const char *canonical_hostname,
	struct sockaddr *peer_addr,
	int *connection_in_progress_p, int *sockp, const char *source_ip)
{
	int rv, sock, save_errno;
	gfarm_error_t e;

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == -1 && (errno == ENFILE || errno == EMFILE)) {
		gfs_client_connection_gc(); /* XXX FIXME: GC all descriptors */
		sock = socket(PF_INET, SOCK_STREAM, 0);
	}
	if (sock == -1) {
		save_errno = errno;
		gflog_debug(GFARM_MSG_1001174,
			"Creation of inet socket failed: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}

	/* XXX - how to report setsockopt(2) failure ? */
	gfarm_sockopt_apply_by_name_addr(sock, canonical_hostname, peer_addr);

	/*
	 * this fcntl should never fail, or even if this fails, that's OK
	 * because its only effect is that TCP timeout becomes longer.
	 */
	fcntl(sock, F_SETFL, O_NONBLOCK);

	if (source_ip != NULL) {
		e = gfarm_bind_source_ip(sock, source_ip);
		if (e != GFARM_ERR_NO_ERROR) {
			close(sock);
			gflog_debug(GFARM_MSG_1001175,
				"bind() with inet socket (%s) failed: %s",
				source_ip,
				gfarm_error_string(e));
			return (e);
		}
	}

	rv = connect(sock, peer_addr, sizeof(*peer_addr));
	if (rv < 0) {
		if (errno != EINPROGRESS) {
			save_errno = errno;
			close(sock);
			gflog_debug(GFARM_MSG_1001176,
				"connect() with inet socket failed: %s",
				strerror(save_errno));
			return (gfarm_errno_to_error(save_errno));
		}
		*connection_in_progress_p = 1;
	} else {
		*connection_in_progress_p = 0;
	}
	fcntl(sock, F_SETFL, 0); /* clear O_NONBLOCK, this should never fail */
	*sockp = sock;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_client_connection_alloc(const char *canonical_hostname,
	struct sockaddr *peer_addr, struct gfp_cached_connection *cache_entry,
	int *connection_in_progress_p, struct gfs_connection **gfs_serverp,
	const char *source_ip)
{
	gfarm_error_t e;
	struct gfs_connection *gfs_server;
	int connection_in_progress, sock = -1, is_local = 0;

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	connection_in_progress = 0;
#endif
	if (sockaddr_is_local(peer_addr)) {
		e = gfs_client_connect_unix(peer_addr, &sock);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001177,
				"connect with unix socket failed: %s",
				gfarm_error_string(e));
			return (e);
		}
		connection_in_progress = 0;
		is_local = 1;
	}
	if (sock == -1) {
		e = gfs_client_connect_inet(canonical_hostname,
		    peer_addr, &connection_in_progress, &sock, source_ip);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001178,
				"connect with inet socket failed: %s",
				gfarm_error_string(e));
			return (e);
		}
	}
	fcntl(sock, F_SETFD, 1); /* automatically close() on exec(2) */

	GFARM_MALLOC(gfs_server);
	if (gfs_server == NULL) {
		close(sock);
		gflog_debug(GFARM_MSG_1001179,
			"allocation of 'gfs_server' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	e = gfp_xdr_new_socket(sock, &gfs_server->conn);
	if (e != GFARM_ERR_NO_ERROR) {
		free(gfs_server);
		close(sock);
		gflog_debug(GFARM_MSG_1001180,
			"gfp_xdr_new_socket() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	gfs_server->hostname = strdup(canonical_hostname);
	if (gfs_server->hostname == NULL) {
		e = gfp_xdr_free(gfs_server->conn);
		free(gfs_server);
		gflog_debug(GFARM_MSG_1001181,
			"allocation of 'gfs_server->hostname' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	gfs_server->port = ntohs(((struct sockaddr_in *)peer_addr)->sin_port);
	gfs_server->is_local = is_local;
	gfs_server->pid = 0;
	gfs_server->context = NULL;
	gfs_server->opened = 0;

	gfs_server->cache_entry = cache_entry;
	gfp_cached_connection_set_data(cache_entry, gfs_server);

	*connection_in_progress_p = connection_in_progress;
	*gfs_serverp = gfs_server;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_client_connection_alloc_and_auth(const char *canonical_hostname,
	const char *user, struct sockaddr *peer_addr,
	struct gfp_cached_connection *cache_entry,
	struct gfs_connection **gfs_serverp, const char *source_ip)
{
	gfarm_error_t e;
	int connection_in_progress;
	struct gfs_connection *gfs_server;

	e = gfs_client_connection_alloc(canonical_hostname, peer_addr,
	    cache_entry, &connection_in_progress, &gfs_server, source_ip);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001182,
			"gfs_client_connection_alloc() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (connection_in_progress)
		e = gfarm_connect_wait(gfp_xdr_fd(gfs_server->conn),
		    GFS_CLIENT_CONNECT_TIMEOUT);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfarm_auth_request(gfs_server->conn, GFS_SERVICE_TAG,
		    gfs_server->hostname, peer_addr, gfarm_get_auth_id_type(),
		    user, &gfs_server->auth_method);
	if (e == GFARM_ERR_NO_ERROR)
		*gfs_serverp = gfs_server;
	else {
		free(gfs_server->hostname);
		gfp_xdr_free(gfs_server->conn);
		free(gfs_server);
		gflog_debug(GFARM_MSG_1001183,
			"connection or authentication failed: %s",
			gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
gfs_client_connection_dispose(void *connection_data)
{
	struct gfs_connection *gfs_server = connection_data;
	gfarm_error_t e = gfp_xdr_free(gfs_server->conn);

	gfp_uncached_connection_dispose(gfs_server->cache_entry);
	free(gfs_server->hostname);
	/* XXX - gfs_server->context should be NULL here */
	free(gfs_server);
	return (e);
}

/*
 * gfs_client_connection_free() can be used for both 
 * an uncached connection which was created by gfs_client_connect(), and
 * a cached connection which was created by gfs_client_connection_acquire().
 * The connection will be immediately closed in the former uncached case.
 * 
 */
void
gfs_client_connection_free(struct gfs_connection *gfs_server)
{
	gfp_cached_or_uncached_connection_free(&gfs_server_cache,
	    gfs_server->cache_entry);
}

void
gfs_client_terminate(void)
{
	gfp_cached_connection_terminate(&gfs_server_cache);
}

/*
 * gfs_client_connection_acquire - create or lookup a cached connection
 */
gfarm_error_t
gfs_client_connection_acquire(const char *canonical_hostname, const char *user,
	struct sockaddr *peer_addr, struct gfs_connection **gfs_serverp)
{
	gfarm_error_t e;
	struct gfp_cached_connection *cache_entry;
	int created;

	e = gfp_cached_connection_acquire(&gfs_server_cache,
	    canonical_hostname,
	    ntohs(((struct sockaddr_in *)peer_addr)->sin_port),
	    user, &cache_entry, &created);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001184,
			"acquirement of cached connection (%s) failed: %s",
			canonical_hostname,
			gfarm_error_string(e));
		return (e);
	}
	if (!created) {
		*gfs_serverp = gfp_cached_connection_get_data(cache_entry);
		return (GFARM_ERR_NO_ERROR);
	}
	e = gfs_client_connection_alloc_and_auth(canonical_hostname, user,
	    peer_addr, cache_entry, gfs_serverp, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		gfp_cached_connection_purge_from_cache(&gfs_server_cache,
		    cache_entry);
		gfp_uncached_connection_dispose(cache_entry);
		gflog_debug(GFARM_MSG_1001185,
			"allocation or authentication failed: %s",
			gfarm_error_string(e));
	}
	return (e);
}

gfarm_error_t
gfs_client_connection_acquire_by_host(struct gfm_connection *gfm_server,
	const char *canonical_hostname, int port,
	struct gfs_connection **gfs_serverp, const char *source_ip)
{
	gfarm_error_t e;
	struct gfp_cached_connection *cache_entry;
	int created;
	struct sockaddr peer_addr;
	const char *user = gfm_client_username(gfm_server);

	/*
	 * lookup gfs_server_cache first,
	 * to eliminate hostname -> IP-address conversion in a cached case.
	 */
	e = gfp_cached_connection_acquire(&gfs_server_cache,
	    canonical_hostname, port, user, &cache_entry, &created);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001186,
			"acquirement of cached connection (%s) failed: %s",
			canonical_hostname,
			gfarm_error_string(e));
		return (e);
	}
	if (!created) {
		*gfs_serverp = gfp_cached_connection_get_data(cache_entry);
		return (GFARM_ERR_NO_ERROR);
	}
	e = gfm_host_address_get(gfm_server, canonical_hostname, port,
	    &peer_addr, NULL);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfs_client_connection_alloc_and_auth(canonical_hostname,
		    user, &peer_addr, cache_entry, gfs_serverp, source_ip);
	if (e != GFARM_ERR_NO_ERROR) {
		gfp_cached_connection_purge_from_cache(&gfs_server_cache,
		    cache_entry);
		gfp_uncached_connection_dispose(cache_entry);
		gflog_debug(GFARM_MSG_1001187,
			"client authentication failed: %s",
			gfarm_error_string(e));
	}
	return (e);
}

/*
 * gfs_client_connect - create an uncached connection
 *
 * XXX FIXME
 * `hostname' to `addr' conversion really should be done in this function,
 * rather than a caller of this function.
 */
gfarm_error_t
gfs_client_connect(const char *canonical_hostname, int port, const char *user,
	struct sockaddr *peer_addr, struct gfs_connection **gfs_serverp)
{
	gfarm_error_t e;
	struct gfp_cached_connection *cache_entry;

	e = gfp_uncached_connection_new(canonical_hostname, port, user,
		&cache_entry);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001188,
			"making new uncached connection failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	e = gfs_client_connection_alloc_and_auth(canonical_hostname, user,
	    peer_addr, cache_entry, gfs_serverp, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		gfp_uncached_connection_dispose(cache_entry);
		gflog_debug(GFARM_MSG_1001189,
			"client authentication failed: %s",
			gfarm_error_string(e));
	}
	return (e);
}

/* convert from uncached connection to cached */
gfarm_error_t
gfs_client_connection_enter_cache(struct gfs_connection *gfs_server)
{
	if (gfs_client_connection_is_cached(gfs_server)) {
		gflog_error(GFARM_MSG_1000068,
		    "gfs_client_connection_enter_cache: "
		    "programming error");
		abort();
	}
	return (gfp_uncached_connection_enter_cache(&gfs_server_cache,
	    gfs_server->cache_entry));
}

/*
 * multiplexed version of gfs_client_connect() for parallel authentication
 * for parallel authentication
 */

struct gfs_client_connect_state {
	struct gfarm_eventqueue *q;
	struct gfarm_event *writable;
	struct sockaddr peer_addr;
	void (*continuation)(void *);
	void *closure;

	struct gfs_connection *gfs_server;

	struct gfarm_auth_request_state *auth_state;

	/* results */
	gfarm_error_t error;
};

static void
gfs_client_connect_end_auth(void *closure)
{
	struct gfs_client_connect_state *state = closure;

	state->error = gfarm_auth_result_multiplexed(
	    state->auth_state,
	    &state->gfs_server->auth_method);
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfs_client_connect_start_auth(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct gfs_client_connect_state *state = closure;
	int error;
	socklen_t error_size = sizeof(error);
	int rv = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_size);

	if (rv == -1) { /* Solaris, see UNP by rstevens */
		state->error = gfarm_errno_to_error(errno);
	} else if (error != 0) {
		state->error = gfarm_errno_to_error(error);
	} else { /* successfully connected */
		state->error = gfarm_auth_request_multiplexed(state->q,
		    state->gfs_server->conn, GFS_SERVICE_TAG,
		    state->gfs_server->hostname,
		    &state->peer_addr,
		    gfarm_get_auth_id_type(),
		    gfs_client_username(state->gfs_server),
		    gfs_client_connect_end_auth, state,
		    &state->auth_state);
		if (state->error == GFARM_ERR_NO_ERROR) {
			/*
			 * call auth_request,
			 * then go to gfs_client_connect_end_auth()
			 */
			return;
		}
	}
	gflog_debug(GFARM_MSG_1001190,
		"starting client connection auth failed: %s",
		gfarm_error_string(state->error));
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

gfarm_error_t
gfs_client_connect_request_multiplexed(struct gfarm_eventqueue *q,
	const char *canonical_hostname, int port, const char *user,
	struct sockaddr *peer_addr,
	void (*continuation)(void *), void *closure,
	struct gfs_client_connect_state **statepp)
{
	gfarm_error_t e;
	int rv;
	struct gfp_cached_connection *cache_entry;
	struct gfs_connection *gfs_server;
	struct gfs_client_connect_state *state;
	int connection_in_progress;

	/* clone of gfs_client_connect() */

	GFARM_MALLOC(state);
	if (state == NULL) {
		gflog_debug(GFARM_MSG_1001191,
			"allocation of client connect state failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	e = gfp_uncached_connection_new(canonical_hostname,
	    port, user, &cache_entry);
	if (e != GFARM_ERR_NO_ERROR) {
		free(state);
		gflog_debug(GFARM_MSG_1001192,
			"making new uncached connection failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	e = gfs_client_connection_alloc(canonical_hostname, peer_addr,
	    cache_entry, &connection_in_progress, &gfs_server, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		gfp_uncached_connection_dispose(cache_entry);
		free(state);
		gflog_debug(GFARM_MSG_1001193,
			"allocation of client connection failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	state->q = q;
	state->peer_addr = *peer_addr;
	state->continuation = continuation;
	state->closure = closure;
	state->gfs_server = gfs_server;
	state->auth_state = NULL;
	state->error = GFARM_ERR_NO_ERROR;
	if (connection_in_progress) {
		state->writable = gfarm_fd_event_alloc(GFARM_EVENT_WRITE,
		    gfp_xdr_fd(gfs_server->conn),
		    gfs_client_connect_start_auth, state);
		if (state->writable == NULL) {
			e = GFARM_ERR_NO_MEMORY;
		} else if ((rv = gfarm_eventqueue_add_event(q, state->writable,
		    NULL)) == 0) {
			*statepp = state;
			/* go to gfs_client_connect_start_auth() */
			return (GFARM_ERR_NO_ERROR);
		} else {
			e = gfarm_errno_to_error(rv);
			gfarm_event_free(state->writable);
		}
	} else {
		state->writable = NULL;
		e = gfarm_auth_request_multiplexed(q,
		    gfs_server->conn, GFS_SERVICE_TAG,
		    gfs_server->hostname, &state->peer_addr,
		    gfarm_get_auth_id_type(), user,
		    gfs_client_connect_end_auth, state,
		    &state->auth_state);
		if (e == GFARM_ERR_NO_ERROR) {
			*statepp = state;
			/*
			 * call gfarm_auth_request,
			 * then go to gfs_client_connect_end_auth()
			 */
			return (GFARM_ERR_NO_ERROR);
		}
	}
	free(state);
	gfs_client_connection_dispose(gfs_server);
	gflog_debug(GFARM_MSG_1001194,
		"request for multiplexed client connect failed: %s",
		gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfs_client_connect_result_multiplexed(
	struct gfs_client_connect_state *state,
	struct gfs_connection **gfs_serverp)
{
	gfarm_error_t e = state->error;
	struct gfs_connection *gfs_server = state->gfs_server;

	if (state->writable != NULL)
		gfarm_event_free(state->writable);
	free(state);
	if (e != GFARM_ERR_NO_ERROR) {
		gfs_client_connection_dispose(gfs_server);
		gflog_debug(GFARM_MSG_1001195,
			"error in result of multiplexed client connect: %s",
			gfarm_error_string(e));
		return (e);
	}

	*gfs_serverp = gfs_server;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * gfs_client RPC
 */

int
gfarm_fd_receive_message(int fd, void *buf, size_t size,
	int fdc, int *fdv)
{
	char *buffer = buf;
	int i, rv;
	struct iovec iov[1];
	struct msghdr msg;
#ifdef HAVE_MSG_CONTROL /* 4.3BSD Reno or later */
	struct {
		struct cmsghdr hdr;
		char data[CMSG_SPACE(sizeof(*fdv) * GFSD_MAX_PASSING_FD)
			  - sizeof(struct cmsghdr)];
	} cmsg;

	if (fdc > GFSD_MAX_PASSING_FD) {
#if 0
		fprintf(stderr, "gfarm_fd_receive_message(%s): "
			"fd count %d > %d\n", fdc, GFSD_MAX_PASSING_FD);
#endif
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
#ifndef HAVE_MSG_CONTROL
		if (fdc > 0) {
			msg.msg_accrights = (caddr_t)fdv;
			msg.msg_accrightslen = sizeof(*fdv) * fdc;
			for (i = 0; i < fdc; i++)
				fdv[i] = -1;
		} else {
			msg.msg_accrights = NULL;
			msg.msg_accrightslen = 0;
		}
#else /* 4.3BSD Reno or later */
		if (fdc > 0) {
			msg.msg_control = (caddr_t)&cmsg.hdr;
			msg.msg_controllen = CMSG_SPACE(sizeof(*fdv) * fdc);
			memset(msg.msg_control, 0, msg.msg_controllen);
			for (i = 0; i < fdc; i++)
				((int *)CMSG_DATA(&cmsg.hdr))[i] = -1;
		} else {
			msg.msg_control = NULL;
			msg.msg_controllen = 0;
		}
#endif
		rv = recvmsg(fd, &msg, 0);
		if (rv == -1) {
			if (errno == EINTR)
				continue;
			return (errno); /* failure */
		} else if (rv == 0) {
			return (-1); /* EOF */
		}
#ifdef HAVE_MSG_CONTROL /* 4.3BSD Reno or later */
		if (fdc > 0) {
			if (msg.msg_controllen !=
			    CMSG_SPACE(sizeof(*fdv) * fdc) ||
			    cmsg.hdr.cmsg_len !=
			    CMSG_LEN(sizeof(*fdv) * fdc) ||
			    cmsg.hdr.cmsg_level != SOL_SOCKET ||
			    cmsg.hdr.cmsg_type != SCM_RIGHTS) {
#if 0
				fprintf(stderr,
					"gfarm_fd_receive_message():"
					" descriptor not passed"
					" msg_controllen: %d (%d),"
					" cmsg_len: %d (%d),"
					" cmsg_level: %d,"
					" cmsg_type: %d\n",
					msg.msg_controllen,
					CMSG_SPACE(sizeof(*fdv) * fdc),
					cmsg.hdr.cmsg_len,
					CMSG_LEN(sizeof(*fdv) * fdc),
					cmsg.hdr.cmsg_level,
					cmsg.hdr.cmsg_type);
#endif
			}
			for (i = 0; i < fdc; i++)
				fdv[i] = ((int *)CMSG_DATA(&cmsg.hdr))[i];
		}
#endif
		fdc = 0; fdv = NULL;
		buffer += rv;
		size -= rv;
	}
	return (0); /* success */
}

gfarm_error_t
gfs_client_rpc_request(struct gfs_connection *gfs_server, int command,
	const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfp_xdr_vrpc_request(gfs_server->conn, command, &format, &ap);
	va_end(ap);
	if (IS_CONNECTION_ERROR(e)) {
		gfs_client_execute_hook_for_connection_error(gfs_server);
		gfs_client_purge_from_cache(gfs_server);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001196,
			"gfp_xdr_vrpc_request() failed: %s",
			gfarm_error_string(e));
	}
	return (e);
}

gfarm_error_t
gfs_client_rpc_result(struct gfs_connection *gfs_server, int just,
	const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	int errcode;

	gfs_client_connection_used(gfs_server);

	e = gfp_xdr_flush(gfs_server->conn);
	if (IS_CONNECTION_ERROR(e)) {
		gfs_client_execute_hook_for_connection_error(gfs_server);
		gfs_client_purge_from_cache(gfs_server);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001197,
			"gfp_xdr_flush() failed : %s",
			gfarm_error_string(e));
		return (e);
	}

	va_start(ap, format);
	e = gfp_xdr_vrpc_result(gfs_server->conn, just,
				  &errcode, &format, &ap);
	va_end(ap);

	if (IS_CONNECTION_ERROR(e)) {
		gfs_client_execute_hook_for_connection_error(gfs_server);
		gfs_client_purge_from_cache(gfs_server);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001198,
			"gfp_xdr_vrpc_result() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (errcode != 0) {
		/*
		 * We just use gfarm_error_t as the errcode,
		 * Note that GFARM_ERR_NO_ERROR == 0.
		 */
		gflog_debug(GFARM_MSG_1001199,
			"gfp_xdr_vrpc_result() failed errcode=%d",
			errcode);
		return (errcode);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_rpc(struct gfs_connection *gfs_server, int just, int command,
	const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	int errcode;

	gfs_client_connection_used(gfs_server);

	va_start(ap, format);
	e = gfp_xdr_vrpc(gfs_server->conn, just,
	    command, &errcode, &format, &ap);
	va_end(ap);

	if (IS_CONNECTION_ERROR(e)) {
		gfs_client_execute_hook_for_connection_error(gfs_server);
		gfs_client_purge_from_cache(gfs_server);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001200,
			"gfp_xdr_vrpc() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (errcode != 0) {
		/*
		 * We just use gfarm_error_t as the errcode,
		 * Note that GFARM_ERR_NO_ERROR == 0.
		 */
		gflog_debug(GFARM_MSG_1001201,
			"gfp_xdr_vrpc() errcode=%d",
			errcode);
		return (errcode);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_process_set(struct gfs_connection *gfs_server,
	gfarm_int32_t type, const char *key, size_t size, gfarm_pid_t pid)
{
	gfarm_error_t e;

	e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_PROCESS_SET, "ibl/",
	    type, size, key, pid);
	if (e == GFARM_ERR_NO_ERROR)
		gfs_server->pid = pid;
	else
		gflog_debug(GFARM_MSG_1001202,
			"gfs_client_rpc() failed: %s",
			gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfs_client_open(struct gfs_connection *gfs_server, gfarm_int32_t fd)
{
	gfarm_error_t e;

	e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_OPEN, "i/", fd);
	if (e == GFARM_ERR_NO_ERROR)
		++gfs_server->opened;
	else
		gflog_debug(GFARM_MSG_1001203,
			"gfs_client_rpc() failed: %s",
			gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfs_client_open_local(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	int *fd_ret)
{
	gfarm_error_t e;
	int rv, local_fd;
	gfarm_int8_t dummy; /* needs at least 1 byte */

	if (!gfs_server->is_local) {
		gflog_debug(GFARM_MSG_1001204,
			"gfs server is local: %s",
			gfarm_error_string(GFARM_ERR_OPERATION_NOT_SUPPORTED));
		return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
	}

	/* we have to set `just' flag here */
	e = gfs_client_rpc(gfs_server, 1, GFS_PROTO_OPEN_LOCAL, "i/", fd);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001205,
			"gfs_client_rpc() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	/* layering violation, but... */
	rv = gfarm_fd_receive_message(gfp_xdr_fd(gfs_server->conn),
	    &dummy, sizeof(dummy), 1, &local_fd);
	if (rv == -1) { /* EOF */
		gflog_debug(GFARM_MSG_1001206,
			"Unexpected EOF when receiving message: %s",
			gfarm_error_string(GFARM_ERR_UNEXPECTED_EOF));
		return (GFARM_ERR_UNEXPECTED_EOF);
	}
	if (rv != 0) {
		gflog_debug(GFARM_MSG_1001207,
			"receiving message failed: %s",
			gfarm_error_string(gfarm_errno_to_error(rv)));
		return (gfarm_errno_to_error(rv));
	}
	/* both `dummy' and `local_fd` are passed by using host byte order. */
	*fd_ret = local_fd;
	++gfs_server->opened;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_close(struct gfs_connection *gfs_server, gfarm_int32_t fd)
{
	gfarm_error_t e;

	e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_CLOSE, "i/", fd);
	if (e == GFARM_ERR_NO_ERROR)
		--gfs_server->opened;
	else
		gflog_debug(GFARM_MSG_1001208,
			"gfs_client_rpc() failed: %s",
			gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfs_client_pread(struct gfs_connection *gfs_server,
	gfarm_int32_t fd, void *buffer, size_t size,
	gfarm_off_t off, size_t *np)
{
	gfarm_error_t e;

	if ((e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_PREAD, "iil/b",
	    fd, (int)size, off,
	    size, np, buffer)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001209,
			"gfs_client_rpc() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (*np > size) {
		gflog_debug(GFARM_MSG_1001210,
			"Protocol error in client pread (%llu)>(%llu)",
			(unsigned long long)*np, (unsigned long long)size);
		return (GFARM_ERRMSG_GFS_PROTO_PREAD_PROTOCOL);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_pwrite(struct gfs_connection *gfs_server,
	gfarm_int32_t fd, const void *buffer, size_t size,
	gfarm_off_t off,
	size_t *np)
{
	gfarm_error_t e;
	gfarm_int32_t n; /* size_t may be 64bit */

	if ((e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_PWRITE, "ibl/i",
	    fd, size, buffer, off, &n)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001211,
			"gfs_client_rpc() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	*np = n;
	if (n > size) {
		gflog_debug(GFARM_MSG_1001212,
			"Protocol error in client pwrite (%llu)>(%llu)",
			(unsigned long long)*np, (unsigned long long)size);
		return (GFARM_ERRMSG_GFS_PROTO_PWRITE_PROTOCOL);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_ftruncate(struct gfs_connection *gfs_server,
	gfarm_int32_t fd, gfarm_off_t size)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_FTRUNCATE, "il/",
	    fd, size));
}

gfarm_error_t
gfs_client_fsync(struct gfs_connection *gfs_server,
	gfarm_int32_t fd, gfarm_int32_t op)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_FSYNC, "ii/",
	    fd, op));
}

gfarm_error_t
gfs_client_fstat(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	gfarm_off_t *size,
	gfarm_int64_t *atime_sec, gfarm_int32_t *atime_nsec,
	gfarm_int64_t *mtime_sec, gfarm_int32_t *mtime_nsec)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_FSTAT, "i/llili",
	    fd, size, atime_sec, atime_nsec, mtime_sec, mtime_nsec));
}

gfarm_error_t
gfs_client_cksum_set(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	const char *cksum_type, size_t length, const char *cksum)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_CKSUM_SET, "isb/",
	    fd, cksum_type, length, cksum));
}

gfarm_error_t
gfs_client_lock(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	gfarm_off_t start, gfarm_off_t len,
	gfarm_int32_t type, gfarm_int32_t whence)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_LOCK, "illii/",
	    fd, start, len, type, whence));
}

gfarm_error_t
gfs_client_trylock(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	gfarm_off_t start, gfarm_off_t len,
	gfarm_int32_t type, gfarm_int32_t whence)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_TRYLOCK, "illii/",
	    fd, start, len, type, whence));
}

gfarm_error_t
gfs_client_unlock(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	gfarm_off_t start, gfarm_off_t len,
	gfarm_int32_t type, gfarm_int32_t whence)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_UNLOCK, "illii/",
	    fd, start, len, type, whence));
}

gfarm_error_t
gfs_client_lock_info(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	gfarm_off_t start, gfarm_off_t len,
	gfarm_int32_t type, gfarm_int32_t whence,
	gfarm_off_t *start_ret, gfarm_off_t *len_ret,
	gfarm_int32_t *type_ret, char **host_ret, gfarm_pid_t **pid_ret)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_LOCK_INFO,
	    "illii/llisl",
	    fd, start, len, type, whence,
	    start_ret, len_ret, type_ret, host_ret, pid_ret));
}

gfarm_error_t
gfs_client_replica_add_from(struct gfs_connection *gfs_server,
	char *host, gfarm_int32_t port, gfarm_int32_t fd)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_REPLICA_ADD_FROM,
	    "sii/", host, port, fd));
}

gfarm_error_t
gfs_client_statfs(struct gfs_connection *gfs_server, char *path,
	gfarm_int32_t *bsizep,
	gfarm_off_t *blocksp, gfarm_off_t *bfreep, gfarm_off_t *bavailp,
	gfarm_off_t *filesp, gfarm_off_t *ffreep, gfarm_off_t *favailp)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_STATFS, "s/illllll",
	    path, bsizep, blocksp, bfreep, bavailp, filesp, ffreep, favailp));
}

/*
 * multiplexed version of gfs_client_statfs()
 */
struct gfs_client_statfs_state {
	struct gfarm_eventqueue *q;
	struct gfarm_event *writable, *readable;
	void (*continuation)(void *);
	void *closure;
	struct gfs_connection *gfs_server;
	char *path;

	/* results */
	gfarm_error_t error;
	gfarm_int32_t bsize;
	gfarm_off_t blocks, bfree, bavail;
	gfarm_off_t files, ffree, favail;
};

static void
gfs_client_statfs_send_request(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct gfs_client_statfs_state *state = closure;
	int rv;
	struct timeval timeout;

	state->error = gfs_client_rpc_request(state->gfs_server,
	    GFS_PROTO_STATFS, "s", state->path);
	if (state->error == GFARM_ERR_NO_ERROR) {
		state->error = gfp_xdr_flush(state->gfs_server->conn);
		if (IS_CONNECTION_ERROR(state->error)) {
			gfs_client_execute_hook_for_connection_error(
			    state->gfs_server);
			gfs_client_purge_from_cache(state->gfs_server);
		}
		if (state->error == GFARM_ERR_NO_ERROR) {
			timeout.tv_sec = GFS_CLIENT_COMMAND_TIMEOUT;
			timeout.tv_usec = 0;
			if ((rv = gfarm_eventqueue_add_event(state->q,
			    state->readable, &timeout)) == 0) {
				/* go to gfs_client_statfs_recv_result() */
				return;
			}
			state->error = gfarm_errno_to_error(rv);
		}
	}
	gflog_debug(GFARM_MSG_1001213,
		"request for client statfs failed: %s",
		gfarm_error_string(state->error));
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfs_client_statfs_recv_result(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct gfs_client_statfs_state *state = closure;

	if ((events & GFARM_EVENT_TIMEOUT) != 0) {
		assert(events == GFARM_EVENT_TIMEOUT);
		state->error = GFARM_ERR_OPERATION_TIMED_OUT;
	} else {
		assert(events == GFARM_EVENT_READ);
		state->error = gfs_client_rpc_result(state->gfs_server, 0,
		    "illllll", &state->bsize,
		    &state->blocks, &state->bfree, &state->bavail,
		    &state->files, &state->ffree, &state->favail);
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

gfarm_error_t
gfs_client_statfs_request_multiplexed(struct gfarm_eventqueue *q,
	struct gfs_connection *gfs_server, char *path,
	void (*continuation)(void *), void *closure,
	struct gfs_client_statfs_state **statepp)
{
	gfarm_error_t e;
	int rv;
	struct gfs_client_statfs_state *state;

	GFARM_MALLOC(state);
	if (state == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001214,
			"allocation of client statfs state failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		goto error_return;
	}

	state->q = q;
	state->continuation = continuation;
	state->closure = closure;
	state->gfs_server = gfs_server;
	state->path = path;
	state->error = GFARM_ERR_NO_ERROR;
	state->writable = gfarm_fd_event_alloc(GFARM_EVENT_WRITE,
	    gfs_client_connection_fd(gfs_server),
	    gfs_client_statfs_send_request, state);
	if (state->writable == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001215,
			"allocation of state->writable failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		goto error_free_state;
	}
	/*
	 * We cannot use two independent events (i.e. a fd_event with
	 * GFARM_EVENT_READ flag and a timer_event) here, because
	 * it's possible that both event handlers are called at once.
	 */
	state->readable = gfarm_fd_event_alloc(
	    GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT,
	    gfs_client_connection_fd(gfs_server),
	    gfs_client_statfs_recv_result, state);
	if (state->readable == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001216,
			"allocation of state->readable failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		goto error_free_writable;
	}
	/* go to gfs_client_statfs_send_request() */
	rv = gfarm_eventqueue_add_event(q, state->writable, NULL);
	if (rv != 0) {
		e = gfarm_errno_to_error(rv);
		gflog_debug(GFARM_MSG_1001217,
			"adding event to event queue failed: %s",
			gfarm_error_string(e));
		goto error_free_readable;
	}
	*statepp = state;
	return (GFARM_ERR_NO_ERROR);
		
error_free_readable:
	gfarm_event_free(state->readable);
error_free_writable:
	gfarm_event_free(state->writable);
error_free_state:
	free(state);
error_return:
	return (e);
}

gfarm_error_t
gfs_client_statfs_result_multiplexed(struct gfs_client_statfs_state *state,
	gfarm_int32_t *bsizep,
	gfarm_off_t *blocksp, gfarm_off_t *bfreep, gfarm_off_t *bavailp,
	gfarm_off_t *filesp, gfarm_off_t *ffreep, gfarm_off_t *favailp)
{
	gfarm_error_t e = state->error;

	gfarm_event_free(state->writable);
	gfarm_event_free(state->readable);
	if (e == GFARM_ERR_NO_ERROR) {
		*bsizep = state->bsize;
		*blocksp = state->blocks;
		*bfreep = state->bfree;
		*bavailp = state->bavail;
		*filesp = state->files;
		*ffreep = state->ffree;
		*favailp = state->favail;
	}
	free(state);
	return (e);
}


/*
 * GFS_PROTO_REPLICA_RECV is only used by gfsd,
 * but defined here for better maintainability.
 */

gfarm_error_t
gfs_client_replica_recv(struct gfs_connection *gfs_server,
	gfarm_ino_t ino, gfarm_uint64_t gen, gfarm_int32_t local_fd)
{
	gfarm_error_t e, e_write = GFARM_ERR_NO_ERROR, e_rpc;
	int i, rv, eof;
	char buffer[GFS_PROTO_MAX_IOSIZE];

	e = gfs_client_rpc_request(gfs_server, GFS_PROTO_REPLICA_RECV, "ll",
	    ino, gen);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(gfs_server->conn);
	if (IS_CONNECTION_ERROR(e)) {
		gfs_client_execute_hook_for_connection_error(gfs_server);
		gfs_client_purge_from_cache(gfs_server);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001218,
			"gfs_request_client_rpc() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	for (;;) {
		gfarm_int32_t size;
		int skip = 0;

		/* XXX - FIXME layering violation */
		e = gfp_xdr_recv(gfs_server->conn, 0, &eof, "i", &size);
		if (IS_CONNECTION_ERROR(e)) {
			gfs_client_execute_hook_for_connection_error(
			    gfs_server);
			gfs_client_purge_from_cache(gfs_server);
		}
		if (e != GFARM_ERR_NO_ERROR)
			break;
		if (eof) {
			e = GFARM_ERR_PROTOCOL;
			break;
		}
		if (size <= 0)
			break;
		do {
			/* XXX - FIXME layering violation */
			int partial = gfp_xdr_recv_partial(gfs_server->conn, 0,
				buffer, size);

			e = gfp_xdr_recv_get_error(gfs_server->conn);
			if (IS_CONNECTION_ERROR(e)) {
				gfs_client_execute_hook_for_connection_error(
				    gfs_server);
				gfs_client_purge_from_cache(gfs_server);
			}
			if (e != GFARM_ERR_NO_ERROR)
				break;
			if (partial <= 0) {
				e = GFARM_ERR_PROTOCOL;
				break;
			}
			size -= partial;
#ifdef __GNUC__ /* shut up stupid warning by gcc */
			rv = 0;
#endif
			i = 0;
			if (skip) /* write(2) returns error */
				i = partial;
			for (; i < partial; i += rv) {
				rv = write(local_fd, buffer + i, partial - i);
				if (rv <= 0)
					break;
			}
			if (i < partial) {
				/*
				 * write(2) never returns 0,
				 * so the following rv == 0 case is
				 * just warm fuzzy.
				 */
				e_write = gfarm_errno_to_error(
						rv == 0 ? ENOSPC : errno);
				/*
				 * we should receive rest of data,
				 * even if write(2) fails.
				 */
				skip = 1;
			}
		} while (size > 0);
		if (e != GFARM_ERR_NO_ERROR)
			break;
	}
	e_rpc = gfs_client_rpc_result(gfs_server, 0, "");
	if (e == GFARM_ERR_NO_ERROR)
		e = e_write;
	if (e == GFARM_ERR_NO_ERROR)
		e = e_rpc;
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001219,
			"receiving client replica failed: %s",
			gfarm_error_string(e));
	}
	return (e);
}

/*
 **********************************************************************
 * Implementation of gfs_client_command()
 **********************************************************************
 */

struct gfs_client_command_context {
	struct gfarm_iobuffer *iobuffer[NFDESC];

	enum { GFS_COMMAND_SERVER_STATE_NEUTRAL,
		       GFS_COMMAND_SERVER_STATE_OUTPUT,
		       GFS_COMMAND_SERVER_STATE_EXITED,
		       GFS_COMMAND_SERVER_STATE_ABORTED }
		server_state;
	int server_output_fd;
	int server_output_residual;
	enum { GFS_COMMAND_CLIENT_STATE_NEUTRAL,
		       GFS_COMMAND_CLIENT_STATE_OUTPUT }
		client_state;
	int client_output_residual;

	int pid;
	int pending_signal;
};

void
gfs_client_command_set_stdin(struct gfs_connection *gfs_server,
	int (*rf)(struct gfarm_iobuffer *, void *, int, void *, int),
	void *cookie, int fd)
{
	struct gfs_client_command_context *cc = gfs_server->context;

	gfarm_iobuffer_set_read(cc->iobuffer[FDESC_STDIN], rf, cookie, fd);
}

void
gfs_client_command_set_stdout(struct gfs_connection *gfs_server,
	int (*wf)(struct gfarm_iobuffer *, void *, int, void *, int),
	void (*wcf)(struct gfarm_iobuffer *, void *, int),
	void *cookie, int fd)
{
	struct gfs_client_command_context *cc = gfs_server->context;

	gfarm_iobuffer_set_write(cc->iobuffer[FDESC_STDOUT], wf, cookie, fd);
	gfarm_iobuffer_set_write_close(cc->iobuffer[FDESC_STDOUT], wcf);

}

void
gfs_client_command_set_stderr(struct gfs_connection *gfs_server,
	int (*wf)(struct gfarm_iobuffer *, void *, int, void *, int),
	void (*wcf)(struct gfarm_iobuffer *, void *, int),
	void *cookie, int fd)
{
	struct gfs_client_command_context *cc = gfs_server->context;

	gfarm_iobuffer_set_write(cc->iobuffer[FDESC_STDERR], wf, cookie, fd);
	gfarm_iobuffer_set_write_close(cc->iobuffer[FDESC_STDERR], wcf);
}

gfarm_error_t
gfs_client_command_request(struct gfs_connection *gfs_server,
	char *path, char **argv, char **envp,
	int flags, int *pidp)
{
	struct gfs_client_command_context *cc;
	int na = argv == NULL ? 0 : gfarm_strarray_length(argv);
	int ne = envp == NULL ? 0 : gfarm_strarray_length(envp);
	int conn_fd = gfp_xdr_fd(gfs_server->conn);
	int i, xenv_copy, xauth_copy;
	gfarm_int32_t pid;
	socklen_t siz;
	gfarm_error_t e;

	static char *xdisplay_name_cache = NULL;
	static char *xdisplay_env_cache = NULL;
	static int xauth_cached = 0;
	static char *xauth_cache = NULL;
	char *dpy;

	if ((dpy = getenv("DISPLAY")) != NULL && xdisplay_name_cache == NULL &&
	    (flags & GFS_CLIENT_COMMAND_FLAG_X11MASK) != 0) {
		/*
		 * get $DISPLAY to `xdisplay_name_cache',
		 * and set "DISPLAY=$DISPLAY" to `xdisplay_env_cache'.
		 */
		static char xdisplay_env_format[] = "DISPLAY=%s";
		static char local_prefix[] = "unix:";
		char *prefix;

		if (*dpy == ':') {
			prefix = gfarm_host_get_self_name();
		} else if (memcmp(dpy, local_prefix,
				  sizeof(local_prefix) - 1) == 0) {
			prefix = gfarm_host_get_self_name();
			dpy += sizeof(local_prefix) - 1 - 1;
		} else {
			prefix = "";
		}
		GFARM_MALLOC_ARRAY(xdisplay_name_cache,
			strlen(prefix) + strlen(dpy) + 1);
		if (xdisplay_name_cache == NULL) {
			gflog_debug(GFARM_MSG_1001220,
				"allocation of array 'xdisplay_name_cache' "
				"failed: %s",
				gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}
		sprintf(xdisplay_name_cache, "%s%s", prefix, dpy);
		GFARM_MALLOC_ARRAY(xdisplay_env_cache,
		    sizeof(xdisplay_env_format) + strlen(xdisplay_name_cache));
		if (xdisplay_env_cache == NULL) {
			free(xdisplay_name_cache);
			xdisplay_name_cache = NULL;
			gflog_debug(GFARM_MSG_1001221,
				"allocation of array 'xdisplay_env_cache' "
				"failed : %s",
				gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}
		sprintf(xdisplay_env_cache, xdisplay_env_format,
			xdisplay_name_cache);

	}
	if ((flags & GFS_CLIENT_COMMAND_FLAG_X11MASK) ==
	    GFS_CLIENT_COMMAND_FLAG_XAUTHCOPY &&
	    xdisplay_name_cache != NULL && !xauth_cached) {
		/*
		 * get xauth data to `xauth_cache'
		 */
		static char xauth_command_format[] =
			"%s nextract - %s 2>/dev/null";
		char *xauth_command;
		FILE *fp;
		char *s, line[XAUTH_NEXTRACT_MAXLEN];

		GFARM_MALLOC_ARRAY(xauth_command, 
			sizeof(xauth_command_format) +
			strlen(XAUTH_COMMAND) +
			strlen(xdisplay_name_cache));
		if (xauth_command == NULL) {
			gflog_debug(GFARM_MSG_1001222,
				"allocation of array 'xauth_command' "
				"failed : %s",
				gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}
		sprintf(xauth_command, xauth_command_format,
			XAUTH_COMMAND, xdisplay_name_cache);
		if ((fp = popen(xauth_command, "r")) == NULL) {
			free(xauth_command);
			gflog_debug(GFARM_MSG_1001223,
				"popen() for auth command pipe failed: %s",
				gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}
		s = fgets(line, sizeof line, fp);
		pclose(fp);
		free(xauth_command);
		if (s != NULL) {
			xauth_cache = strdup(line);
			if (xauth_cache == NULL) {
				free(xdisplay_name_cache);
				gflog_debug(GFARM_MSG_1001224,
					"allocation of 'xauth_cache' "
					"failed : %s",
					gfarm_error_string(
						GFARM_ERR_NO_MEMORY));
				return (GFARM_ERR_NO_MEMORY);
			}
		}
		xauth_cached = 1;
	}

	xenv_copy =
		(flags & GFS_CLIENT_COMMAND_FLAG_X11MASK) != 0 &&
		xdisplay_env_cache != NULL;
	xauth_copy =
		(flags & GFS_CLIENT_COMMAND_FLAG_X11MASK) ==
		GFS_CLIENT_COMMAND_FLAG_XAUTHCOPY &&
		xauth_cache != NULL;

	/*
	 * don't pass
	 * GFS_CLIENT_COMMAND_FLAG_STDIN_EOF and
	 * GFS_CLIENT_COMMAND_FLAG_XENV_COPY flag via network
	 */
	e = gfs_client_rpc_request(gfs_server,
		GFS_PROTO_COMMAND, "siii",
		path, na,
		ne + (xenv_copy ? 1 : 0),
		((flags & GFS_CLIENT_COMMAND_FLAG_SHELL_COMMAND) ?
		GFS_CLIENT_COMMAND_FLAG_SHELL_COMMAND : 0) |
		(xauth_copy ? GFS_CLIENT_COMMAND_FLAG_XAUTHCOPY : 0));
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001225,
			"gfs_request_client_rpc() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	/*
	 * argv
	 */
	for (i = 0; i < na; i++) {
		e = gfp_xdr_send(gfs_server->conn, "s", argv[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001226,
				"sending argv[%d] (%s) failed: %s",
				i, argv[i],
				gfarm_error_string(e));
			return (e);
		}
	}
	/*
	 * envp
	 */
	for (i = 0; i < ne; i++) {
		e = gfp_xdr_send(gfs_server->conn, "s", envp[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001227,
				"sending envp[%d] (%s) failed: %s",
				i, envp[i],
				gfarm_error_string(e));
			return (e);
		}
	}
	if (xenv_copy) {
		e = gfp_xdr_send(gfs_server->conn, "s", xdisplay_env_cache);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001228,
				"sending xdisplay_env_cache failed: %s",
				gfarm_error_string(e));
			return (e);
		}
	}
	if (xauth_copy) {
		/*
		 * xauth
		 */
		e = gfp_xdr_send(gfs_server->conn, "s", xauth_cache);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001229,
				"sending xauth_cache failed: %s",
				gfarm_error_string(e));
			return (e);
		}
	}
	/* we have to set `just' flag here */
	e = gfs_client_rpc_result(gfs_server, 1, "i", &pid);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001230,
			"gfs_client_rpc_result() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	gfs_server->context = GFARM_MALLOC(cc);
	if (gfs_server->context == NULL) {
		gflog_debug(GFARM_MSG_1001231,
			"allocation of 'gfs_server->context' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	/*
	 * Now, we set the connection file descriptor non-blocking mode.
	 */
	if (fcntl(conn_fd, F_SETFL, O_NONBLOCK) == -1) {
		int save_errno = errno;
		free(gfs_server->context);
		gfs_server->context = NULL;
		gflog_debug(GFARM_MSG_1001232,
			"setting conn_fd to non-blocking mode failed: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}

	/*
	 * initialize gfs_server->context by default values
	 */
	siz = sizeof(i);
	if (getsockopt(conn_fd, SOL_SOCKET, SO_SNDBUF, &i, &siz))
		i = GFARM_DEFAULT_COMMAND_IOBUF_SIZE;
	cc->iobuffer[FDESC_STDIN] = gfarm_iobuffer_alloc(i);

	siz = sizeof(i);
	if (getsockopt(conn_fd, SOL_SOCKET, SO_RCVBUF, &i, &siz))
		i = GFARM_DEFAULT_COMMAND_IOBUF_SIZE;
	cc->iobuffer[FDESC_STDOUT] = gfarm_iobuffer_alloc(i);
	cc->iobuffer[FDESC_STDERR] = gfarm_iobuffer_alloc(i);

	gfarm_iobuffer_set_nonblocking_read_fd(
		cc->iobuffer[FDESC_STDIN], FDESC_STDIN);
	gfarm_iobuffer_set_nonblocking_write_xxx(
		cc->iobuffer[FDESC_STDIN], gfs_server->conn);

	gfarm_iobuffer_set_nonblocking_read_xxx(
		cc->iobuffer[FDESC_STDOUT], gfs_server->conn);
	gfarm_iobuffer_set_nonblocking_write_fd(
		cc->iobuffer[FDESC_STDOUT], FDESC_STDOUT);

	gfarm_iobuffer_set_nonblocking_read_xxx(
		cc->iobuffer[FDESC_STDERR], gfs_server->conn);
	gfarm_iobuffer_set_nonblocking_write_fd(
		cc->iobuffer[FDESC_STDERR], FDESC_STDERR);

	if ((flags & GFS_CLIENT_COMMAND_FLAG_STDIN_EOF) != 0)
		gfarm_iobuffer_set_read_eof(cc->iobuffer[FDESC_STDIN]);

	cc->server_state = GFS_COMMAND_SERVER_STATE_NEUTRAL;
	cc->client_state = GFS_COMMAND_CLIENT_STATE_NEUTRAL;
	cc->pending_signal = 0;

	*pidp = cc->pid = pid;

	return (e);
}

int
gfs_client_command_is_running(struct gfs_connection *gfs_server)
{
	struct gfs_client_command_context *cc = gfs_server->context;

	return (cc->server_state != GFS_COMMAND_SERVER_STATE_EXITED &&
		cc->server_state != GFS_COMMAND_SERVER_STATE_ABORTED);
}

gfarm_error_t
gfs_client_command_fd_set(struct gfs_connection *gfs_server,
			  fd_set *readable, fd_set *writable, int *max_fdp)
{
	struct gfs_client_command_context *cc = gfs_server->context;
	int conn_fd = gfp_xdr_fd(gfs_server->conn);
	int i, fd;

	/*
	 * The following test condition should just match with
	 * the i/o condition in gfs_client_command_io_fd_set(),
	 * otherwise unneeded busy wait happens.
	 */

	if (cc->server_state == GFS_COMMAND_SERVER_STATE_NEUTRAL ||
	    (cc->server_state == GFS_COMMAND_SERVER_STATE_OUTPUT &&
	     gfarm_iobuffer_is_readable(cc->iobuffer[cc->server_output_fd]))) {
		FD_SET(conn_fd, readable);
		if (*max_fdp < conn_fd)
			*max_fdp = conn_fd;
	}
	if ((cc->client_state == GFS_COMMAND_CLIENT_STATE_NEUTRAL &&
	     (cc->pending_signal ||
	      gfarm_iobuffer_is_writable(cc->iobuffer[FDESC_STDIN]))) ||
	    cc->client_state == GFS_COMMAND_CLIENT_STATE_OUTPUT) {
		FD_SET(conn_fd, writable);
		if (*max_fdp < conn_fd)
			*max_fdp = conn_fd;
	}

	if (gfarm_iobuffer_is_readable(cc->iobuffer[FDESC_STDIN])) {
		fd = gfarm_iobuffer_get_read_fd(cc->iobuffer[FDESC_STDIN]);
		if (fd < 0) {
			gfarm_iobuffer_read(cc->iobuffer[FDESC_STDIN], NULL);
			/* XXX - if the callback sets an error? */
		} else {
			FD_SET(fd, readable);
			if (*max_fdp < fd)
				*max_fdp = fd;
		}
	}

	for (i = FDESC_STDOUT; i <= FDESC_STDERR; i++) {
		if (gfarm_iobuffer_is_writable(cc->iobuffer[i])) {
			fd = gfarm_iobuffer_get_write_fd(cc->iobuffer[i]);
			if (fd < 0) {
				gfarm_iobuffer_write(cc->iobuffer[i], NULL);
				/* XXX - if the callback sets an error? */
			} else {
				FD_SET(fd, writable);
				if (*max_fdp < fd)
					*max_fdp = fd;
			}
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_command_io_fd_set(struct gfs_connection *gfs_server,
			     fd_set *readable, fd_set *writable)
{
	gfarm_error_t e;
	struct gfs_client_command_context *cc = gfs_server->context;
	int i, fd, conn_fd = gfp_xdr_fd(gfs_server->conn);

	fd = gfarm_iobuffer_get_read_fd(cc->iobuffer[FDESC_STDIN]);
	if (fd >= 0 && FD_ISSET(fd, readable)) {
		gfarm_iobuffer_read(cc->iobuffer[FDESC_STDIN], NULL);
		e = gfarm_iobuffer_get_error(cc->iobuffer[FDESC_STDIN]);
		if (e != GFARM_ERR_NO_ERROR) {
			/* treat this as eof */
			gfarm_iobuffer_set_read_eof(cc->iobuffer[FDESC_STDIN]);
			/* XXX - how to report this error? */
			gfarm_iobuffer_set_error(cc->iobuffer[FDESC_STDIN],
			    GFARM_ERR_NO_ERROR);
		}
	}

	for (i = FDESC_STDOUT; i <= FDESC_STDERR; i++) {
		fd = gfarm_iobuffer_get_write_fd(cc->iobuffer[i]);
		if (fd < 0 || !FD_ISSET(fd, writable))
			continue;
		gfarm_iobuffer_write(cc->iobuffer[i], NULL);
		e = gfarm_iobuffer_get_error(cc->iobuffer[i]);
		if (e == GFARM_ERR_NO_ERROR)
			continue;
		/* XXX - just purge the content */
		gfarm_iobuffer_purge(cc->iobuffer[i], NULL);
		/* XXX - how to report this error? */
		gfarm_iobuffer_set_error(cc->iobuffer[i], GFARM_ERR_NO_ERROR);
	}

	if (FD_ISSET(conn_fd, readable)) {
		if (cc->server_state == GFS_COMMAND_SERVER_STATE_NEUTRAL) {
			gfarm_int32_t cmd, fd, len;
			int eof;
			gfarm_error_t e;

			e = gfp_xdr_recv(gfs_server->conn, 1, &eof,
					   "i", &cmd);
			if (e != GFARM_ERR_NO_ERROR || eof) {
				if (e == GFARM_ERR_NO_ERROR)
					e = GFARM_ERRMSG_GFSD_ABORTED;
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_ABORTED;
				gflog_debug(GFARM_MSG_1001233,
					"gfp_xdr_recv() failed: %s",
					gfarm_error_string(e));
				return (e);
			}
			switch (cmd) {
			case GFS_PROTO_COMMAND_EXITED:
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_EXITED;
				break;
			case GFS_PROTO_COMMAND_FD_OUTPUT:
				e = gfp_xdr_recv(gfs_server->conn, 1, &eof,
						   "ii", &fd, &len);
				if (e != GFARM_ERR_NO_ERROR || eof) {
					if (e == GFARM_ERR_NO_ERROR)
						e = GFARM_ERRMSG_GFSD_ABORTED;
					cc->server_state =
					    GFS_COMMAND_SERVER_STATE_ABORTED;
					gflog_debug(GFARM_MSG_1001234,
						"gfp_xdr_recv() failed: "
						"%s",
						gfarm_error_string(e));
					return (e);
				}
				if (fd != FDESC_STDOUT && fd != FDESC_STDERR) {
					/* XXX - something wrong */
					cc->server_state =
					    GFS_COMMAND_SERVER_STATE_ABORTED;
					gflog_debug(GFARM_MSG_1001235,
						"Illegal file descriptor");
					return (GFARM_ERRMSG_GFSD_DESCRIPTOR_ILLEGAL);
				}
				if (len <= 0) {
					/* notify closed */
					gfarm_iobuffer_set_read_eof(
					    cc->iobuffer[fd]);
				} else {
					cc->server_state =
					    GFS_COMMAND_SERVER_STATE_OUTPUT;
					cc->server_output_fd = fd;
					cc->server_output_residual = len;
				}
				break;
			default:
				/* XXX - something wrong */
				cc->server_state =
				    GFS_COMMAND_SERVER_STATE_ABORTED;
				gflog_debug(GFARM_MSG_1001236,
					"Unknown reply %d", cmd);
				return (GFARM_ERRMSG_GFSD_REPLY_UNKNOWN);
			}
		} else if (cc->server_state==GFS_COMMAND_SERVER_STATE_OUTPUT) {
			gfarm_iobuffer_read(cc->iobuffer[cc->server_output_fd],
				&cc->server_output_residual);
			if (cc->server_output_residual == 0)
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_NEUTRAL;
			e = gfarm_iobuffer_get_error(
			    cc->iobuffer[cc->server_output_fd]);
			if (e != GFARM_ERR_NO_ERROR) {
				/* treat this as eof */
				gfarm_iobuffer_set_read_eof(
				    cc->iobuffer[cc->server_output_fd]);
				gfarm_iobuffer_set_error(
				    cc->iobuffer[cc->server_output_fd],
				    GFARM_ERR_NO_ERROR);
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_ABORTED;
				gflog_debug(GFARM_MSG_1001237,
					"read operation on iobuffer failed: %s",
					gfarm_error_string(e));
				return (e);
			}
			if (gfarm_iobuffer_is_read_eof(
					cc->iobuffer[cc->server_output_fd])) {
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_ABORTED;
				gflog_debug(GFARM_MSG_1001238,
					"read operation on iobuffer aborted");
				return (GFARM_ERRMSG_GFSD_ABORTED);
			}
		}
	}
	if (FD_ISSET(conn_fd, writable) &&
	    gfs_client_command_is_running(gfs_server)) {
		if (cc->client_state == GFS_COMMAND_CLIENT_STATE_NEUTRAL) {
			if (cc->pending_signal) {
				e = gfp_xdr_send(gfs_server->conn, "ii",
					GFS_PROTO_COMMAND_SEND_SIGNAL,
					cc->pending_signal);
				if (e != GFARM_ERR_NO_ERROR ||
				    (e = gfp_xdr_flush(gfs_server->conn))
				    != GFARM_ERR_NO_ERROR) {
					cc->server_state =
					    GFS_COMMAND_SERVER_STATE_ABORTED;
					gflog_debug(GFARM_MSG_1001239,
						"sending signal failed: %s",
						gfarm_error_string(e));
					return (e);
				}
			} else if (gfarm_iobuffer_is_writable(
			    cc->iobuffer[FDESC_STDIN])) {
				/*
				 * cc->client_output_residual may be 0,
				 * if stdin reaches EOF.
				 */
				cc->client_output_residual =
					gfarm_iobuffer_avail_length(
					    cc->iobuffer[FDESC_STDIN]);
				e = gfp_xdr_send(gfs_server->conn, "iii",
					GFS_PROTO_COMMAND_FD_INPUT,
					FDESC_STDIN,
					cc->client_output_residual);
				if (e != GFARM_ERR_NO_ERROR ||
				    (e = gfp_xdr_flush(gfs_server->conn))
				    != GFARM_ERR_NO_ERROR) {
					cc->server_state =
					    GFS_COMMAND_SERVER_STATE_ABORTED;
					gflog_debug(GFARM_MSG_1001240,
						"sending fd falied: %s",
						gfarm_error_string(e));
					return (e);
				}
				cc->client_state =
				    GFS_COMMAND_CLIENT_STATE_OUTPUT;
			}
		} else if (cc->client_state==GFS_COMMAND_CLIENT_STATE_OUTPUT) {
			gfarm_iobuffer_write(cc->iobuffer[FDESC_STDIN],
				&cc->client_output_residual);
			if (cc->client_output_residual == 0)
				cc->client_state =
					GFS_COMMAND_CLIENT_STATE_NEUTRAL;
			e = gfarm_iobuffer_get_error(
			    cc->iobuffer[FDESC_STDIN]);
			if (e != GFARM_ERR_NO_ERROR) {
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_ABORTED;
				gfarm_iobuffer_set_error(
				    cc->iobuffer[FDESC_STDIN],
				    GFARM_ERR_NO_ERROR);
				gflog_debug(GFARM_MSG_1001241,
					"write operation on iobuffer "
					"failed: %s",
					gfarm_error_string(e));
				return (e);
			}
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_command_io(struct gfs_connection *gfs_server,
		      struct timeval *timeout)
{
	int nfound, max_fd;
	fd_set readable, writable;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	if (!gfs_client_command_is_running(gfs_server))
		return (GFARM_ERR_NO_ERROR);

	max_fd = -1;
	FD_ZERO(&readable);
	FD_ZERO(&writable);

	gfs_client_command_fd_set(gfs_server, &readable, &writable, &max_fd);
	if (max_fd >= 0) {
		nfound = select(max_fd + 1,
				&readable, &writable, NULL, timeout);
		if (nfound > 0) {
			e = gfs_client_command_io_fd_set(gfs_server,
							 &readable, &writable);
		} else if (nfound == -1 && errno != EINTR) {
			e = gfarm_errno_to_error(errno);
		}
	}

	return (e);
}

int
gfs_client_command_send_stdin(struct gfs_connection *gfs_server,
			      void *data, int len)
{
	struct gfs_client_command_context *cc = gfs_server->context;
	char *p = data;
	int residual = len, rv;

	while (residual > 0 && gfs_client_command_is_running(gfs_server)) {
		rv = gfarm_iobuffer_put(cc->iobuffer[FDESC_STDIN],
					p, residual);
		p += rv;
		residual -= rv;
		gfs_client_command_io(gfs_server, NULL);
		/* XXX - how to report this error? */
	}
	return (len - residual);
}

void
gfs_client_command_close_stdin(struct gfs_connection *gfs_server)
{
	struct gfs_client_command_context *cc = gfs_server->context;

	gfarm_iobuffer_set_read_eof(cc->iobuffer[FDESC_STDIN]);
}

gfarm_error_t
gfs_client_command_send_signal(struct gfs_connection *gfs_server, int sig)
{
	gfarm_error_t e;
	struct gfs_client_command_context *cc = gfs_server->context;

	while (gfs_client_command_is_running(gfs_server) &&
	       cc->pending_signal != 0) {
		e = gfs_client_command_io(gfs_server, NULL);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001242,
				"gfs_client_command_io() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
	}
	if (!gfs_client_command_is_running(gfs_server))
		return (gfarm_errno_to_error(ESRCH));
	cc->pending_signal = sig;
	/* make a chance to send the signal immediately */
	return (gfs_client_command_io(gfs_server, NULL));
}

gfarm_error_t
gfs_client_command_result(struct gfs_connection *gfs_server,
	int *term_signal, int *exit_status, int *exit_flag)
{
	struct gfs_client_command_context *cc = gfs_server->context;
	gfarm_error_t e;

	while (gfs_client_command_is_running(gfs_server)) {
		gfs_client_command_io(gfs_server, NULL);
		/* XXX - how to report this error? */
	}

	/*
	 * flush stdout/stderr
	 */
	while (gfarm_iobuffer_is_writable(cc->iobuffer[FDESC_STDOUT]) ||
	       gfarm_iobuffer_is_writable(cc->iobuffer[FDESC_STDERR])) {
		int i, nfound, fd, max_fd = -1;
		fd_set writable;

		FD_ZERO(&writable);
		for (i = FDESC_STDOUT; i <= FDESC_STDERR; i++) {
			if (gfarm_iobuffer_is_writable(cc->iobuffer[i])) {
				fd = gfarm_iobuffer_get_write_fd(
				    cc->iobuffer[i]);
				if (fd < 0) {
					gfarm_iobuffer_write(cc->iobuffer[i],
					    NULL);
					/*
					 * XXX - if the callback sets an error?
					 */
				} else {
					FD_SET(fd, &writable);
					if (max_fd < fd)
						max_fd = fd;
				}
			}
		}

		if (max_fd < 0)
			continue;

		nfound = select(max_fd + 1, NULL, &writable, NULL, NULL);
		if (nfound == -1 && errno != EINTR)
			break;

		if (nfound > 0) {
			for (i = FDESC_STDOUT; i <= FDESC_STDERR; i++) {
				fd = gfarm_iobuffer_get_write_fd(
				    cc->iobuffer[i]);
				if (fd < 0 || !FD_ISSET(fd, &writable))
					continue;
				gfarm_iobuffer_write(cc->iobuffer[i], NULL);
				e = gfarm_iobuffer_get_error(cc->iobuffer[i]);
				if (e == GFARM_ERR_NO_ERROR)
					continue;
				/* XXX - just purge the content */
				gfarm_iobuffer_purge(cc->iobuffer[i], NULL);
				/* XXX - how to report this error? */
				gfarm_iobuffer_set_error(cc->iobuffer[i],
				    GFARM_ERR_NO_ERROR);
			}
		}
	}

	/*
	 * context isn't needed anymore
	 */
	gfarm_iobuffer_free(cc->iobuffer[FDESC_STDIN]);
	gfarm_iobuffer_free(cc->iobuffer[FDESC_STDOUT]);
	gfarm_iobuffer_free(cc->iobuffer[FDESC_STDERR]);
	free(gfs_server->context);
	gfs_server->context = NULL;
	/*
	 * Now, we recover the connection file descriptor blocking mode.
	 */
	if (fcntl(gfp_xdr_fd(gfs_server->conn), F_SETFL, 0) == -1) {
		int save_errno = errno;
		gflog_debug(GFARM_MSG_1001243,
			"setting conn fd to blocking mode failed: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_COMMAND_EXIT_STATUS,
			       "/iii",
			       term_signal, exit_status, exit_flag));
}

gfarm_error_t
gfs_client_command(struct gfs_connection *gfs_server,
	char *path, char **argv, char **envp, int flags,
	int *term_signal, int *exit_status, int *exit_flag)
{
	gfarm_error_t e, e2;
	int pid;

	e = gfs_client_command_request(gfs_server, path, argv, envp, flags,
				       &pid);
	if (e) {
		gflog_debug(GFARM_MSG_1001244,
			"gfs_client_command_request() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	while (gfs_client_command_is_running(gfs_server))
		e = gfs_client_command_io(gfs_server, NULL);
	e2 = gfs_client_command_result(gfs_server,
				       term_signal, exit_status, exit_flag);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001245,
			"gfs_client_command_io() failed: %s",
			gfarm_error_string(e));
	} else if (e2 != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001246,
			"gfs_client_command_result() failed: %s",
			gfarm_error_string(e2));
	}
	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

/*
 **********************************************************************
 * gfsd datagram service
 **********************************************************************
 */

int gfs_client_datagram_timeouts[] = { /* milli seconds */
	8000, 12000
};
int gfs_client_datagram_ntimeouts =
	GFARM_ARRAY_LENGTH(gfs_client_datagram_timeouts);

/*
 * `server_addr_size' should be socklen_t, but that requires <sys/socket.h>
 * for all source files which include "gfs_client.h".
 */
gfarm_error_t
gfs_client_get_load_request(int sock,
	struct sockaddr *server_addr, int server_addr_size)
{
	int rv, command = 0;

	if (server_addr == NULL || server_addr_size == 0) {
		/* using connected UDP socket */
		rv = write(sock, &command, sizeof(command));
	} else {
		rv = sendto(sock, &command, sizeof(command), 0,
		    server_addr, server_addr_size);
	}
	if (rv == -1) {
		int save_errno = errno;
		gflog_debug(GFARM_MSG_1001247,
			"write or send operation on socket failed: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	return (GFARM_ERR_NO_ERROR);
}

/*
 * `*server_addr_sizep' is an IN/OUT parameter.
 *
 * `*server_addr_sizep' should be socklen_t, but that requires <sys/socket.h>
 * for all source files which include "gfs_client.h".
 */
gfarm_error_t
gfs_client_get_load_result(int sock,
	struct sockaddr *server_addr, socklen_t *server_addr_sizep,
	struct gfs_client_load *result)
{
	int rv;
	double loadavg[3];
#ifndef WORDS_BIGENDIAN
	struct { char c[8]; } nloadavg[3];
#else
#	define nloadavg loadavg
#endif

	if (server_addr == NULL || server_addr_sizep == NULL) {
		/* caller doesn't need server_addr */
		rv = read(sock, nloadavg, sizeof(nloadavg));
	} else {
		rv = recvfrom(sock, nloadavg, sizeof(nloadavg), 0,
		    server_addr, server_addr_sizep);
	}
	if (rv == -1) {
		int save_errno = errno;
		gflog_debug(GFARM_MSG_1001248,
			"read or receive operation from socket failed: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}

#ifndef WORDS_BIGENDIAN
	swab(&nloadavg[0], &loadavg[0], sizeof(loadavg[0]));
	swab(&nloadavg[1], &loadavg[1], sizeof(loadavg[1]));
	swab(&nloadavg[2], &loadavg[2], sizeof(loadavg[2]));
#endif
	result->loadavg_1min = loadavg[0];
	result->loadavg_5min = loadavg[1];
	result->loadavg_15min = loadavg[2];
	return (GFARM_ERR_NO_ERROR);
}

/*
 * multiplexed version of gfs_client_get_load()
 */

struct gfs_client_get_load_state {
	struct gfarm_eventqueue *q;
	struct gfarm_event *writable, *readable;
	void (*continuation)(void *);
	void *closure;

	int sock;
	int try;

	/* results */
	gfarm_error_t error;
	struct gfs_client_load load;
};

static void
gfs_client_get_load_send(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct gfs_client_get_load_state *state = closure;
	int rv;
	struct timeval timeout;

	state->error = gfs_client_get_load_request(state->sock, NULL, 0);
	if (state->error == GFARM_ERR_NO_ERROR) {
		timeout.tv_sec = timeout.tv_usec = 0;
		gfarm_timeval_add_microsec(&timeout,
		    gfs_client_datagram_timeouts[state->try] *
		    GFARM_MILLISEC_BY_MICROSEC);
		if ((rv = gfarm_eventqueue_add_event(state->q, state->readable,
		    &timeout)) == 0) {
			/* go to gfs_client_get_load_receive() */
			return;
		}
		state->error = gfarm_errno_to_error(rv);
	}
	close(state->sock);
	state->sock = -1;
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfs_client_get_load_receive(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct gfs_client_get_load_state *state = closure;
	int rv;

	if ((events & GFARM_EVENT_TIMEOUT) != 0) {
		assert(events == GFARM_EVENT_TIMEOUT);
		++state->try;
		if (state->try >= gfs_client_datagram_ntimeouts) {
			state->error = GFARM_ERR_OPERATION_TIMED_OUT;
		} else if ((rv = gfarm_eventqueue_add_event(state->q,
		    state->writable, NULL)) == 0) {
			/* go to gfs_client_get_load_send() */
			return;
		} else {
			state->error = gfarm_errno_to_error(rv);
		}
	} else {
		assert(events == GFARM_EVENT_READ);
		state->error = gfs_client_get_load_result(state->sock,
		    NULL, NULL, &state->load);
	}
	close(state->sock);
	state->sock = -1;
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

gfarm_error_t
gfs_client_get_load_request_multiplexed(struct gfarm_eventqueue *q,
	struct sockaddr *peer_addr,
	void (*continuation)(void *), void *closure,
	struct gfs_client_get_load_state **statepp)
{
	gfarm_error_t e;
	int rv, sock;
	struct gfs_client_get_load_state *state;

	/* use different socket for each peer, to identify error code */
	sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock == -1) {
		e = gfarm_errno_to_error(errno);
		goto error_return;
	}
	fcntl(sock, F_SETFD, 1); /* automatically close() on exec(2) */
	/* connect UDP socket, to get error code */
	if (connect(sock, peer_addr, sizeof(*peer_addr)) == -1) {
		e = gfarm_errno_to_error(errno);
		goto error_close_sock;
	}

	GFARM_MALLOC(state);
	if (state == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001249,
			"allocation of client get load state failed: %s",
			gfarm_error_string(e));
		goto error_close_sock;
	}

	state->writable = gfarm_fd_event_alloc(
	    GFARM_EVENT_WRITE, sock,
	    gfs_client_get_load_send, state);
	if (state->writable == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001250,
			"allocation of client get load state->writable "
			"failed: %s",
			gfarm_error_string(e));
		goto error_free_state;
	}
	/*
	 * We cannot use two independent events (i.e. a fd_event with
	 * GFARM_EVENT_READ flag and a timer_event) here, because
	 * it's possible that both event handlers are called at once.
	 */
	state->readable = gfarm_fd_event_alloc(
	    GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT, sock,
	    gfs_client_get_load_receive, state);
	if (state->readable == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001251,
			"allocation of client get load state->readable "
			"failed: %s",
			gfarm_error_string(e));
		goto error_free_writable;
	}
	/* go to gfs_client_get_load_send() */
	rv = gfarm_eventqueue_add_event(q, state->writable, NULL);
	if (rv != 0) {
		e = gfarm_errno_to_error(rv);
		gflog_debug(GFARM_MSG_1001252,
			"adding event to event queue failed: %s",
			gfarm_error_string(e));
		goto error_free_readable;
	}

	state->q = q;
	state->continuation = continuation;
	state->closure = closure;
	state->sock = sock;
	state->try = 0;
	state->error = GFARM_ERR_NO_ERROR;
	*statepp = state;
	return (GFARM_ERR_NO_ERROR);

error_free_readable:
	gfarm_event_free(state->readable);
error_free_writable:
	gfarm_event_free(state->writable);
error_free_state:
	free(state);
error_close_sock:
	close(sock);
error_return:
	return (e);
}

gfarm_error_t
gfs_client_get_load_result_multiplexed(
	struct gfs_client_get_load_state *state, struct gfs_client_load *loadp)
{
	gfarm_error_t error = state->error;

	if (state->sock >= 0) { /* sanity */
		close(state->sock);
		state->sock = -1;
	}
	if (error == GFARM_ERR_NO_ERROR)
		*loadp = state->load;
	gfarm_event_free(state->readable);
	gfarm_event_free(state->writable);
	free(state);
	return (error);
}

#if 0

/*
 * gfs_client_apply_all_hosts()
 */

static int
apply_one_host(char *(*op)(struct gfs_connection *, void *),
	char *hostname, void *args, char *message, int tolerant)
{
	char *e;
	int pid;
	struct sockaddr addr;
	struct gfs_connection *conn;

	pid = fork();
	if (pid) {
		/* parent or error */
		return pid;
	}
	/* child */

	/* reflect "address_use" directive in the `hostname' */
	e = gfarm_host_address_get(hostname, gfarm_spool_server_port, &addr,
		NULL);
	if (e != NULL) {
		if (message != NULL)
			fprintf(stderr, "%s: host %s: %s\n",
			    message, hostname, e);
		_exit(2);
	}

	e = gfs_client_connect(hostname, &addr, &conn);
	if (e != NULL) {
		/* if tolerant, we allow failure to connect the host */
		if (message != NULL && !tolerant)
			fprintf(stderr, "%s: connecting to %s: %s\n", message,
			    hostname, e);
		_exit(tolerant ? 0 : 3);
	}

	e = (*op)(conn, args);
	if (e != NULL) {
		/* if tolerant, we allow "no such file or directory" */
		if (message != NULL &&
		    (!tolerant || e != GFARM_ERR_NO_SUCH_OBJECT))
			fprintf(stderr, "%s on %s: %s\n", message, hostname, e);
		_exit(tolerant && e == GFARM_ERR_NO_SUCH_OBJECT ? 0 : 4);
	}

	e = gfs_client_disconnect(conn);
	if (e != NULL) {
		if (message != NULL)
			fprintf(stderr, "%s: disconnecting to %s: %s\n",
			    message, hostname, e);
		_exit(5);
	}

	_exit(0);
}

static char *
wait_pid(int pids[], int num, int *nhosts_succeed)
{
	char *e;
	int rv, s;

	e = NULL;
	while (--num >= 0) {
		while ((rv = waitpid(pids[num], &s, 0)) == -1 &&
		        errno == EINTR)
				;
		if (rv == -1) {
			if (e == NULL)
				e = gfarm_errno_to_error(errno);
		} else if (WIFEXITED(s)) {
			if (WEXITSTATUS(s) == 0)
				(*nhosts_succeed)++;
			else
				e = "error happened on the operation";
		} else {
			e = "operation aborted abnormally";
		}
	}
	return (e);
}

#define CONCURRENCY	25

static int
gfarm_fd_receive_message(int fd, void *buf, size_t size,
	int fdc, int *fdv)
{
	char *buffer = buf;
	int i, rv;
	struct iovec iov[1];
	struct msghdr msg;
#ifdef HAVE_MSG_CONTROL /* 4.3BSD Reno or later */
	struct {
		struct cmsghdr hdr;
		char data[CMSG_SPACE(sizeof(*fdv) * GFSD_MAX_PASSING_FD)
			  - sizeof(struct cmsghdr)];
	} cmsg;

	if (fdc > GFSD_MAX_PASSING_FD) {
#if 0
		fprintf(stderr, "gfarm_fd_receive_message(%s): "
			"fd count %d > %d\n", fdc, GFSD_MAX_PASSING_FD);
#endif
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
#ifndef HAVE_MSG_CONTROL
		if (fdc > 0) {
			msg.msg_accrights = (caddr_t)fdv;
			msg.msg_accrightslen = sizeof(*fdv) * fdc;
			for (i = 0; i < fdc; i++)
				fdv[i] = -1;
		} else {
			msg.msg_accrights = NULL;
			msg.msg_accrightslen = 0;
		}
#else /* 4.3BSD Reno or later */
		if (fdc > 0) {
			msg.msg_control = (caddr_t)&cmsg.hdr;
			msg.msg_controllen = CMSG_SPACE(sizeof(*fdv) * fdc);
			memset(msg.msg_control, 0, msg.msg_controllen);
			for (i = 0; i < fdc; i++)
				((int *)CMSG_DATA(&cmsg.hdr))[i] = -1;
		} else {
			msg.msg_control = NULL;
			msg.msg_controllen = 0;
		}
#endif
		rv = recvmsg(fd, &msg, 0);
		if (rv == -1) {
			if (errno == EINTR)
				continue;
			return (errno); /* failure */
		} else if (rv == 0) {
			return (-1); /* EOF */
		}
#ifdef HAVE_MSG_CONTROL /* 4.3BSD Reno or later */
		if (fdc > 0) {
			if (msg.msg_controllen !=
			    CMSG_SPACE(sizeof(*fdv) * fdc) ||
			    cmsg.hdr.cmsg_len !=
			    CMSG_LEN(sizeof(*fdv) * fdc) ||
			    cmsg.hdr.cmsg_level != SOL_SOCKET ||
			    cmsg.hdr.cmsg_type != SCM_RIGHTS) {
#if 0
				fprintf(stderr,
					"gfarm_fd_receive_message():"
					" descriptor not passed"
					" msg_controllen: %d (%d),"
					" cmsg_len: %d (%d),"
					" cmsg_level: %d,"
					" cmsg_type: %d\n",
					msg.msg_controllen,
					CMSG_SPACE(sizeof(*fdv) * fdc),
					cmsg.hdr.cmsg_len,
					CMSG_LEN(sizeof(*fdv) * fdc),
					cmsg.hdr.cmsg_level,
					cmsg.hdr.cmsg_type);
#endif
			}
			for (i = 0; i < fdc; i++)
				fdv[i] = ((int *)CMSG_DATA(&cmsg.hdr))[i];
		}
#endif
		fdc = 0; fdv = NULL;
		buffer += rv;
		size -= rv;
	}
	return (0); /* success */
}

gfarm_error_t
gfs_client_apply_all_hosts(
	char *(*op)(struct gfs_connection *, void *),
	void *args, char *message, int tolerant, int *nhosts_succeed)
{
	char *e;
	int i, j, nhosts, pids[CONCURRENCY];
	struct gfarm_host_info *hosts;

	e = gfarm_host_info_get_all(&nhosts, &hosts);
	if (e != NULL) {
		gflog_debug(GFARM_MSG_1001253,
			"gfarm_host_info_get_all() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

        j = 0;
	*nhosts_succeed = 0;
        for (i = 0; i < nhosts; i++) {
                pids[j] = apply_one_host(op, hosts[i].hostname, args,
		    message, tolerant);
                if (pids[j] < 0) /* fork error */
                        break;
                if (++j == CONCURRENCY) {
                        e = wait_pid(pids, j, nhosts_succeed);
                        j = 0;
                }
        }
        if (j > 0)
                e = wait_pid(pids, j, nhosts_succeed);

	gfarm_host_info_free_all(nhosts, hosts);
	return (e);
}
#endif
