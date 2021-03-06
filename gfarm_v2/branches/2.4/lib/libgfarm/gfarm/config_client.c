#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <gfarm/gfarm.h>

#include "timer.h"
#include "gfutil.h"

#include "liberror.h"
#include "gfs_profile.h"
#include "host.h"
#include "auth.h"
#include "gfpath.h"
#define GFARM_USE_STDIO
#include "config.h"
#include "gfm_client.h"
#include "gfs_proto.h"
#include "gfs_client.h"
#include "lookup.h"

#ifdef HAVE_GSI
static gfarm_error_t
gfarm_set_global_user_by_gsi(struct gfm_connection *gfm_server)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct gfarm_user_info user;
	char *gsi_dn;

	/* Global user name determined by the distinguished name. */
	gsi_dn = gfarm_gsi_client_cred_name();
	if (gsi_dn != NULL) {
		e = gfm_client_user_info_get_by_gsi_dn(gfm_server,
			gsi_dn, &user);
		if (e == GFARM_ERR_NO_ERROR) {
			e = gfm_client_set_username_for_gsi(gfm_server,
			    user.username);
			gfarm_user_info_free(&user);
		} else {
			gflog_debug(GFARM_MSG_1000979,
				"gfm_client_user_info_"
				"get_by_gsi_dn(%s) failed: %s",
				gsi_dn, gfarm_error_string(e));
		}
	}
	return (e);
}
#endif

/*
 * the following function is for client,
 * server/daemon process shouldn't call it.
 * Because this function may read incorrect setting from user specified
 * $USER or $HOME.
 */
gfarm_error_t
gfarm_config_read(void)
{
	gfarm_error_t e;
	char *home;
	FILE *config;
	int lineno, user_config_errno, rc_need_free;;
	static char gfarm_client_rc[] = GFARM_CLIENT_RC;
	char *rc;

	rc_need_free = 0;
	rc = getenv("GFARM_CONFIG_FILE");
	if (rc == NULL) {
		/*
		 * result of gfarm_get_local_homedir() should not be trusted.
		 * (maybe forged)
		 */
		home = gfarm_get_local_homedir();

		GFARM_MALLOC_ARRAY(rc,
		    strlen(home) + 1 + sizeof(gfarm_client_rc));
		if (rc == NULL) {
			gflog_debug(GFARM_MSG_1000980,
				"allocation of array for 'gfarm_client_rc' failed: %s",
				gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}
		sprintf(rc, "%s/%s", home, gfarm_client_rc);
		rc_need_free = 1;
	}
	gfarm_init_config();
	if ((config = fopen(rc, "r")) == NULL) {
		user_config_errno = errno;
	} else {
		user_config_errno = 0;
		e = gfarm_config_read_file(config, &lineno);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1000015, "%s: %d: %s",
			    rc, lineno, gfarm_error_string(e));
			if (rc_need_free)
				free(rc);
			return (e);
		}
	}
	if (rc_need_free)
		free(rc);

	if ((config = fopen(gfarm_config_file, "r")) == NULL) {
		if (user_config_errno != 0) {
			gflog_debug(GFARM_MSG_1000981,
				"open operation on config file (%s) failed",
				gfarm_config_file);
			return (GFARM_ERRMSG_CANNOT_OPEN_CONFIG);
		}
	} else {
		e = gfarm_config_read_file(config, &lineno);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1000016, "%s: %d: %s",
			    gfarm_config_file, lineno, gfarm_error_string(e));
			return (e);
		}
	}

	gfarm_config_set_default_ports();
	gfarm_config_set_default_misc();

	return (GFARM_ERR_NO_ERROR);
}

int gf_on_demand_replication;

static void
gfarm_parse_env_client(void)
{
	char *env;

	if ((env = getenv("GFARM_FLAGS")) != NULL) {
		for (; *env; env++) {
			switch (*env) {
			case 'r': gf_on_demand_replication = 1; break;
			}
		}
	}
}

#if 0 /* not yet in gfarm v2 */

/*
 * redirect stdout
 */

static gfarm_error_t
gfarm_redirect_file(int fd, char *file, GFS_File *gf)
{
	gfarm_error_t e;
	int nfd;

	if (file == NULL)
		return (GFARM_ERR_NO_ERROR);

	e = gfs_pio_create(file, GFARM_FILE_WRONLY, 0644, gf);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	e = gfs_pio_set_view_local(*gf, 0);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	nfd = gfs_pio_fileno(*gf);
	if (nfd == -1)
		return (gfarm_errno_to_error(errno));

	/*
	 * This assumes the file fragment is created in the local
	 * spool.
	 */
	if (dup2(nfd, fd) == -1)
		e = gfarm_errno_to_error(errno);

	/* XXX - apparently violate the layer */
	((struct gfs_file_section_context *)(*gf)->view_context)->fd = fd;
	(*gf)->mode &= ~GFS_FILE_MODE_CALC_DIGEST;

	close(nfd);

	return (e);
}

/*
 * eliminate arguments added by the gfrun command.
 */

static GFS_File gf_stdout, gf_stderr;

gfarm_error_t
gfarm_parse_argv(int *argcp, char ***argvp)
{
	gfarm_error_t e;
	int total_nodes = -1, node_index = -1;
	int argc = *argcp;
	char **argv = *argvp;
	char *argv0 = *argv;
	int call_set_local = 0;
	char *stdout_file = NULL, *stderr_file = NULL;

	--argc;
	++argv;
	while (argc > 0 && argv[0][0] == '-' && argv[0][1] == '-') {
		if (strcmp(&argv[0][2], "gfarm_index") == 0) {
			--argc;
			++argv;
			if (argc > 0)
				node_index = strtol(*argv, NULL, 0);
			call_set_local |= 1;
		}
		else if (strcmp(&argv[0][2], "gfarm_nfrags") == 0) {
			--argc;
			++argv;
			if (argc > 0)
				total_nodes = strtol(*argv, NULL, 0);
			call_set_local |= 2;
		}
		else if (strcmp(&argv[0][2], "gfarm_stdout") == 0) {
			--argc;
			++argv;
			if (argc > 0)
				stdout_file = *argv;
		}
		else if (strcmp(&argv[0][2], "gfarm_stderr") == 0) {
			--argc;
			++argv;
			if (argc > 0)
				stderr_file = *argv;
		}
		else if (strcmp(&argv[0][2], "gfarm_profile") == 0)
			gfs_profile_set();
		else if (strcmp(&argv[0][2], "gfarm_replicate") == 0)
			gf_on_demand_replication = 1;
		else if (strcmp(&argv[0][2], "gfarm_cwd") == 0) {
			--argc;
			++argv;
			if (argc > 0) {
				e = gfs_chdir(*argv);
				if (e != GFARM_ERR_NO_ERROR)
					return (e);
			}
		}
		else
			break;
		--argc;
		++argv;
	}
	if (call_set_local == 3) {
		e = gfs_pio_set_local(node_index, total_nodes);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);

		/* redirect stdout and stderr */
		if (stdout_file != GFARM_ERR_NO_ERROR) {
			e = gfarm_redirect_file(1, stdout_file, &gf_stdout);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
		}
		if (stderr_file != GFARM_ERR_NO_ERROR) {
			e = gfarm_redirect_file(2, stderr_file, &gf_stderr);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
		}

		++argc;
		--argv;

		*argcp = argc;
		*argv = argv0;
		*argvp = argv;
	}	

	return (GFARM_ERR_NO_ERROR);
}

#endif /* not yet in gfarm v2 */

char *gfarm_debug_command;
char gfarm_debug_pid[GFARM_INT64STRLEN + 1];

static int
gfarm_call_debugger(void)
{
	int pid;

	if ((pid = fork()) == 0) {
		execlp("xterm", "xterm", "-e", "gdb",
		       gfarm_debug_command, gfarm_debug_pid, NULL);
		perror("xterm");
		_exit(1);
	}
	return (pid);
}

int
gfarm_attach_debugger(void)
{
	int pid = gfarm_call_debugger();

	/* not really correct way to wait until attached, but... */
	sleep(5);
	return (pid);
}

void
gfarm_sig_debug(int sig)
{
	int rv, pid, status;
	static int already_called = 0;
	static char message[] = "signal 00 caught\n";

	message[7] = sig / 10 + '0';
	message[8] = sig % 10 + '0';
	/* ignore return value, since there is no other way here */
	rv = write(2, message, sizeof(message) - 1);

	if (already_called)
		abort();
	already_called = 1;

	pid = gfarm_call_debugger();
	if (pid == -1) {
		perror("fork"); /* XXX dangerous to call from signal handler */
		abort();
	} else {
		waitpid(pid, &status, 0);
		_exit(1);
	}
}

void
gfarm_debug_initialize(char *command)
{
	gfarm_debug_command = command;
	sprintf(gfarm_debug_pid, "%ld", (long)getpid());

	signal(SIGQUIT, gfarm_sig_debug);
	signal(SIGILL,  gfarm_sig_debug);
	signal(SIGTRAP, gfarm_sig_debug);
	signal(SIGABRT, gfarm_sig_debug);
	signal(SIGFPE,  gfarm_sig_debug);
	signal(SIGBUS,  gfarm_sig_debug);
	signal(SIGSEGV, gfarm_sig_debug);
}

/*
 * the following function is for client,
 * server/daemon process shouldn't call it.
 * Because this function may read incorrect setting from user specified
 * $USER or $HOME.
 */
gfarm_error_t
gfarm_initialize(int *argcp, char ***argvp)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
#ifdef HAVE_GSI
	enum gfarm_auth_method auth_method;
	int saved_auth_verb;
#endif

	gflog_initialize();

	e = gfarm_set_local_user_for_this_local_account();
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000982,
			"gfarm_set_local_user_for_this_local_account() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	e = gfarm_config_read();
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000983,
			"gfarm_config_read() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
#ifdef HAVE_GSI
	/* Force to display verbose error messages. */
	saved_auth_verb = gflog_auth_set_verbose(1);
	(void)gfarm_gsi_client_initialize();
#endif
	/*
	 * this shouldn't be necessary here
	 * to support multiple metadata server
	 */
	e = gfm_client_connection_and_process_acquire_by_path(
	    GFARM_PATH_ROOT, &gfm_server);
#ifdef HAVE_GSI
	(void)gflog_auth_set_verbose(saved_auth_verb);
#endif
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1000017,
		    "connecting to gfmd at %s:%d: %s\n",
		    gfarm_metadb_server_name, gfarm_metadb_server_port,
		    gfarm_error_string(e));
		return (e);
	}

#ifdef HAVE_GSI
	/* metadb access is required to obtain a global user name by GSI */
	auth_method = gfm_client_connection_auth_method(gfm_server);
	if (GFARM_IS_AUTH_GSI(auth_method)) {
		e = gfarm_set_global_user_by_gsi(gfm_server);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1000985,
				"gfarm_set_global_user_by_gsi() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
	}
#endif
	gfm_client_connection_free(gfm_server);

	gfarm_parse_env_client();
	if (argvp != NULL) {
#if 0 /* not yet in gfarm v2 */
		if (getenv("DISPLAY") != NULL)
			gfarm_debug_initialize((*argvp)[0]);
		e = gfarm_parse_argv(argcp, argvp);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
#endif /* not yet in gfarm v2 */
	}

#if 0 /* not yet in gfarm v2 */
	gfarm_initialized = 1;
#endif /* not yet in gfarm v2 */

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_client_process_set(struct gfs_connection *gfs_server,
	struct gfm_connection *gfm_server)
{
	gfarm_int32_t key_type;
	const char *key;
	size_t key_size;
	gfarm_pid_t pid;
	gfarm_error_t e = gfm_client_process_get(gfm_server,
	    &key_type, &key, &key_size, &pid);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000986,
			"gfm_client_process_get() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	return (gfs_client_process_set(gfs_server,
	    key_type, key, key_size, pid));
}

/*
 * the following function is for client,
 * server/daemon process shouldn't call it.
 * Because this function may read incorrect setting from user specified
 * $USER or $HOME.
 */
gfarm_error_t
gfarm_terminate(void)
{
#if 0 /* not yet in gfarm v2 */
	gfarm_error_t e;

	if (gf_stdout != NULL) {
		fflush(stdout);
		e = gfs_pio_close(gf_stdout);
		gf_stdout = NULL;
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	if (gf_stderr != NULL) {
		fflush(stderr);
		e = gfs_pio_close(gf_stderr);
		gf_stderr = NULL;
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
#endif /* not yet in gfarm v2 */
	gfs_profile(gfs_display_timers());
	gfarm_free_config();
	gfs_client_terminate();
	gfm_client_terminate();
	gflog_terminate();

	return (GFARM_ERR_NO_ERROR);
}

#ifdef CONFIG_TEST
main()
{
	gfarm_error_t e;

	e = gfarm_set_local_user_for_this_local_account();
	if (e) {
		fprintf(stderr,
			"gfarm_set_local_user_for_this_local_account(): %s\n",
			e);
		exit(1);
	}
	e = gfarm_config_read();
	if (e) {
		fprintf(stderr, "gfarm_config_read(): %s\n", e);
		exit(1);
	}
	printf("gfarm_spool_root = <%s>\n", gfarm_spool_root);
	printf("gfarm_spool_server_port = <%d>\n", gfarm_spool_server_port);
	printf("gfarm_metadb_server_name = <%s>\n", gfarm_metadb_server_name);
	printf("gfarm_metadb_server_port = <%d>\n", gfarm_metadb_server_name);

	printf("gfarm_ldap_server_name = <%s>\n", gfarm_ldap_server_name);
	printf("gfarm_ldap_server_port = <%s>\n", gfarm_ldap_server_port);
	printf("gfarm_ldap_base_dn = <%s>\n", gfarm_ldap_base_dn);
	return (0);
}
#endif
