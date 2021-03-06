#ifndef GFARM_DEFAULT_FACILITY
#define GFARM_DEFAULT_FACILITY	LOG_LOCAL0
#endif

#ifndef GFARM_DEFAULT_PRIORITY_LEVEL_TO_LOG
#define GFARM_DEFAULT_PRIORITY_LEVEL_TO_LOG	LOG_INFO
#endif

#ifdef __GNUC__
#define GFLOG_PRINTF_ARG(M, N)	__attribute__((__format__(__printf__, M, N)))
#else
#define GFLOG_PRINTF_ARG(M, N)
#endif

#ifdef GFLOG_USE_STDARG /* to make <stdarg.h> optional to use <gfutil.h> */
void gflog_vmessage(int, const char *, va_list) GFLOG_PRINTF_ARG(2, 0);
#endif
void gflog_message(int, const char *, ...) GFLOG_PRINTF_ARG(2, 3);
void gflog_error(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
void gflog_warning(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
void gflog_notice(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
void gflog_info(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
void gflog_debug(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
void gflog_warning_errno(const char *, ...) GFLOG_PRINTF_ARG(1, 2);

void gflog_fatal(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
void gflog_fatal_errno(const char *, ...) GFLOG_PRINTF_ARG(1, 2);

void gflog_set_priority_level(int);
void gflog_set_identifier(const char *);
void gflog_set_auxiliary_info(char *);
char *gflog_get_auxiliary_info(void);
void gflog_syslog_open(int, int);
int gflog_syslog_enabled(void);

int gflog_syslog_name_to_facility(const char *);
int gflog_syslog_name_to_priority(const char *);

/* logutil - gflog_auth_*() */

int gflog_auth_set_verbose(int);
int gflog_auth_get_verbose(void);
void gflog_auth_error(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
void gflog_auth_warning(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
