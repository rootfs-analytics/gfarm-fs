/*
 * address constants which represents error conditions
 *
 * $Id$
 */

/*
 * DO NOT ADD NEW ERROR AT MIDDLE OF THIS DEFINITION.
 * ADD IT AT THE END OF THIS LIST.
 */

enum gfarm_errcode {
 /*  0 */	GFARM_ERR_NO_ERROR,

/* classic errno (1..10) */
 /*  1 */	GFARM_ERR_OPERATION_NOT_PERMITTED, /* forbidden entirely */
 /*  2 */	GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY,
 /*  3 */	GFARM_ERR_NO_SUCH_PROCESS,
 /*  4 */	GFARM_ERR_INTERRUPTED_SYSTEM_CALL,
 /*  5 */	GFARM_ERR_INPUT_OUTPUT,
 /*  6 */	GFARM_ERR_DEVICE_NOT_CONFIGURED,
 /*  7 */	GFARM_ERR_ARGUMENT_LIST_TOO_LONG,
 /*  8 */	GFARM_ERR_EXEC_FORMAT,
 /*  9 */	GFARM_ERR_BAD_FILE_DESCRIPTOR,
 /* 10 */	GFARM_ERR_NO_CHILD_PROCESS,

/* non classic, POSIX (EAGAIN == EWOULDBLOCK) */
 /* 11 */	GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE,

/* classic errno (12..34) */
 /* 12 */	GFARM_ERR_NO_MEMORY,
 /* 13 */	GFARM_ERR_PERMISSION_DENIED, /* prohibited by access control */
 /* 14 */	GFARM_ERR_BAD_ADDRESS,
 /* 15 */	GFARM_ERR_BLOCK_DEVICE_REQUIRED, /* non POSIX, non X/Open */
 /* 16 */	GFARM_ERR_DEVICE_BUSY,
 /* 17 */	GFARM_ERR_ALREADY_EXISTS,
 /* 18 */	GFARM_ERR_CROSS_DEVICE_LINK,
 /* 19 */	GFARM_ERR_OPERATION_NOT_SUPPORTED_BY_DEVICE,
 /* 20 */	GFARM_ERR_NOT_A_DIRECTORY,
 /* 21 */	GFARM_ERR_IS_A_DIRECTORY,
 /* 22 */	GFARM_ERR_INVALID_ARGUMENT,
 /* 23 */	GFARM_ERR_TOO_MANY_OPEN_FILES_IN_SYSTEM,
 /* 24 */	GFARM_ERR_TOO_MANY_OPEN_FILES,
 /* 25 */	GFARM_ERR_INAPPROPRIATE_IOCTL_FOR_DEVICE,
 /* 26 */	GFARM_ERR_TEXT_FILE_BUSY, /* non POSIX */
 /* 27 */	GFARM_ERR_FILE_TOO_LARGE,
 /* 28 */	GFARM_ERR_NO_SPACE,
 /* 29 */	GFARM_ERR_ILLEGAL_SEEK,
 /* 30 */	GFARM_ERR_READ_ONLY_FILE_SYSTEM,
 /* 31 */	GFARM_ERR_TOO_MANY_LINKS,
 /* 32 */	GFARM_ERR_BROKEN_PIPE,
 /* 33 */	GFARM_ERR_NUMERICAL_ARGUMENT_OUT_OF_DOMAIN,
 /* 34 */	GFARM_ERR_RESULT_OUT_OF_RANGE,

/* ISO-C standard amendment, wide/multibyte-character handling */
 /* 35 */	GFARM_ERR_ILLEGAL_BYTE_SEQUENCE,

/* POSIX */
 /* 36 */	GFARM_ERR_RESOURCE_DEADLOCK_AVOIDED,
		GFARM_ERR_FILE_NAME_TOO_LONG,
		GFARM_ERR_DIRECTORY_NOT_EMPTY,
		GFARM_ERR_NO_LOCKS_AVAILABLE,
		GFARM_ERR_FUNCTION_NOT_IMPLEMENTED,

/* X/Open */
		GFARM_ERR_OPERATION_NOW_IN_PROGRESS,
		GFARM_ERR_OPERATION_ALREADY_IN_PROGRESS,
/* X/Open - ipc/network software -- argument errors */
		GFARM_ERR_SOCKET_OPERATION_ON_NON_SOCKET,
		GFARM_ERR_DESTINATION_ADDRESS_REQUIRED,
		GFARM_ERR_MESSAGE_TOO_LONG,
		GFARM_ERR_PROTOCOL_WRONG_TYPE_FOR_SOCKET,
		GFARM_ERR_PROTOCOL_NOT_AVAILABLE,
		GFARM_ERR_PROTOCOL_NOT_SUPPORTED,
		GFARM_ERR_OPERATION_NOT_SUPPORTED,
		GFARM_ERR_ADDRESS_FAMILY_NOT_SUPPORTED_BY_PROTOCOL_FAMILY,
		GFARM_ERR_ADDRESS_ALREADY_IN_USE,
		GFARM_ERR_CANNOT_ASSIGN_REQUESTED_ADDRESS,
/* X/Open - ipc/network software -- operational errors */
		GFARM_ERR_NETWORK_IS_DOWN,
		GFARM_ERR_NETWORK_IS_UNREACHABLE,
		GFARM_ERR_CONNECTION_ABORTED,
		GFARM_ERR_CONNECTION_RESET_BY_PEER,
		GFARM_ERR_NO_BUFFER_SPACE_AVAILABLE,
		GFARM_ERR_SOCKET_IS_ALREADY_CONNECTED,
		GFARM_ERR_SOCKET_IS_NOT_CONNECTED,
		GFARM_ERR_OPERATION_TIMED_OUT,
		GFARM_ERR_CONNECTION_REFUSED,
		GFARM_ERR_NO_ROUTE_TO_HOST,
/* X/Open */
		GFARM_ERR_TOO_MANY_LEVELS_OF_SYMBOLIC_LINK,
		GFARM_ERR_DISK_QUOTA_EXCEEDED,
		GFARM_ERR_STALE_FILE_HANDLE,
/* X/Open - System-V IPC */
		GFARM_ERR_IDENTIFIER_REMOVED,
		GFARM_ERR_NO_MESSAGE_OF_DESIRED_TYPE,
		GFARM_ERR_VALUE_TOO_LARGE_TO_BE_STORED_IN_DATA_TYPE,

/* ipc/network errors */
		GFARM_ERR_AUTHENTICATION,
		GFARM_ERR_EXPIRED,
		GFARM_ERR_PROTOCOL,

/* gfarm specific errors */
		GFARM_ERR_UNKNOWN_HOST,
		GFARM_ERR_CANNOT_RESOLVE_AN_IP_ADDRESS_INTO_A_HOSTNAME,
		GFARM_ERR_NO_SUCH_OBJECT,
		GFARM_ERR_CANT_OPEN,
		GFARM_ERR_UNEXPECTED_EOF,
		GFARM_ERR_GFARM_URL_PREFIX_IS_MISSING,
		GFARM_ERR_TOO_MANY_JOBS,
		GFARM_ERR_FILE_MIGRATED,
		GFARM_ERR_NOT_A_SYMBOLIC_LINK,
		GFARM_ERR_IS_A_SYMBOLIC_LINK,
		GFARM_ERR_UNKNOWN,
		GFARM_ERR_INVALID_FILE_REPLICA,
		GFARM_ERR_NO_SUCH_USER,
		GFARM_ERR_CANNOT_REMOVE_LAST_REPLICA,
		GFARM_ERR_NO_SUCH_GROUP,
		GFARM_ERR_GFARM_URL_USER_IS_MISSING,
		GFARM_ERR_GFARM_URL_HOST_IS_MISSING,
		GFARM_ERR_GFARM_URL_PORT_IS_MISSING,
		GFARM_ERR_GFARM_URL_PORT_IS_INVALID,
		GFARM_ERR_FILE_BUSY,

		GFARM_ERR_NUMBER
};

/* enum gfarm_errcode | enum gfarm_errmsg | enum gfarm_errstat */
typedef int gfarm_error_t;

const char *gfarm_error_string(gfarm_error_t);
gfarm_error_t gfarm_errno_to_error(int);

/* for gfs_hook */
int gfarm_error_to_errno(gfarm_error_t);
