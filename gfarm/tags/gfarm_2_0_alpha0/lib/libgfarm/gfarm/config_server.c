#include <stdio.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfarm_stringlist.h>

#include "gfutil.h"
#include "liberror.h"
#include "auth.h"
#include "config.h"

static void
gfarm_config_set_default_spool_on_server(void)
{
	if (gfarm_spool_root == NULL) {
		/* XXX - this case is not recommended. */
		gfarm_spool_root = GFARM_SPOOL_ROOT;
	}
}

/* the following function is for server. */
gfarm_error_t
gfarm_server_config_read(void)
{
	gfarm_error_t e;
	int lineno;
	FILE *config;
	char lineno_buffer[GFARM_INT64STRLEN + 1];

	gfarm_init_user_map();
	if ((config = fopen(gfarm_config_file, "r")) == NULL) {
		return (GFARM_ERRMSG_CANNOT_OPEN_CONFIG);
	}
	e = gfarm_config_read_file(config, gfarm_config_file, &lineno);
	if (e != GFARM_ERR_NO_ERROR) {
		sprintf(lineno_buffer, "%d", lineno);
		gflog_error(gfarm_error_string(e), lineno_buffer);
		return (e);
	}

	gfarm_config_set_default_ports();

	return (GFARM_ERR_NO_ERROR);
}

/* the following function is for server. */
gfarm_error_t
gfarm_server_initialize(void)
{
	gfarm_error_t e;

	e = gfarm_server_config_read();
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	gfarm_init_user_map();
	gfarm_config_set_default_spool_on_server();

	return (GFARM_ERR_NO_ERROR);
}

/* the following function is for server. */
gfarm_error_t
gfarm_server_terminate(void)
{
	/* nothing to do (and also may never be called) */
	return (GFARM_ERR_NO_ERROR);
}
