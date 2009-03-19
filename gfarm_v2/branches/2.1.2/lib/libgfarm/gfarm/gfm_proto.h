#ifndef GFMD_DEFAULT_PORT
#define GFMD_DEFAULT_PORT	601
#endif

enum gfm_proto_command {
	/* host/user/group metadata */

	GFM_PROTO_HOST_INFO_GET_ALL,
	GFM_PROTO_HOST_INFO_GET_BY_ARCHITECTURE,
	GFM_PROTO_HOST_INFO_GET_BY_NAMES,
	GFM_PROTO_HOST_INFO_GET_BY_NAMEALIASES,
	GFM_PROTO_HOST_INFO_SET,
	GFM_PROTO_HOST_INFO_MODIFY,
	GFM_PROTO_HOST_INFO_REMOVE,
	GFM_PROTO_HOST_INFO_RESERVE7,
	GFM_PROTO_HOST_INFO_RESERVE8,
	GFM_PROTO_HOST_INFO_RESERVE9,
	GFM_PROTO_HOST_INFO_RESERVE10,
	GFM_PROTO_HOST_INFO_RESERVE11,
	GFM_PROTO_HOST_INFO_RESERVE12,
	GFM_PROTO_HOST_INFO_RESERVE13,
	GFM_PROTO_HOST_INFO_RESERVE14,
	GFM_PROTO_HOST_INFO_RESERVE15,

	GFM_PROTO_USER_INFO_GET_ALL,
	GFM_PROTO_USER_INFO_GET_BY_NAMES,
	GFM_PROTO_USER_INFO_SET,
	GFM_PROTO_USER_INFO_MODIFY,
	GFM_PROTO_USER_INFO_REMOVE,
	GFM_PROTO_USER_INFO_GET_BY_GSI_DN,
	GFM_PROTO_USER_INFO_RESERVE6,
	GFM_PROTO_USER_INFO_RESERVE7,
	GFM_PROTO_USER_INFO_RESERVE8,
	GFM_PROTO_USER_INFO_RESERVE9,
	GFM_PROTO_USER_INFO_RESERVE10,
	GFM_PROTO_USER_INFO_RESERVE11,
	GFM_PROTO_USER_INFO_RESERVE12,
	GFM_PROTO_USER_INFO_RESERVE13,
	GFM_PROTO_USER_INFO_RESERVE14,
	GFM_PROTO_USER_INFO_RESERVE15,

	GFM_PROTO_GROUP_INFO_GET_ALL,
	GFM_PROTO_GROUP_INFO_GET_BY_NAMES,
	GFM_PROTO_GROUP_INFO_SET,
	GFM_PROTO_GROUP_INFO_MODIFY,
	GFM_PROTO_GROUP_INFO_REMOVE,
	GFM_PROTO_GROUP_INFO_ADD_USERS,
	GFM_PROTO_GROUP_INFO_REMOVE_USERS,
	GFM_PROTO_GROUP_NAMES_GET_BY_USERS,
	GFM_PROTO_GROUP_INFO_RESERVE8,
	GFM_PROTO_GROUP_INFO_RESERVE9,
	GFM_PROTO_GROUP_INFO_RESERVE10,
	GFM_PROTO_GROUP_INFO_RESERVE11,
	GFM_PROTO_GROUP_INFO_RESERVE12,
	GFM_PROTO_GROUP_INFO_RESERVE13,
	GFM_PROTO_GROUP_INFO_RESERVE14,
	GFM_PROTO_GROUP_INFO_RESERVE15,

	GFM_PROTO_METADATA_RESERVE0,
	GFM_PROTO_METADATA_RESERVE1,
	GFM_PROTO_METADATA_RESERVE2,
	GFM_PROTO_METADATA_RESERVE3,
	GFM_PROTO_METADATA_RESERVE4,
	GFM_PROTO_METADATA_RESERVE5,
	GFM_PROTO_METADATA_RESERVE6,
	GFM_PROTO_METADATA_RESERVE7,
	GFM_PROTO_METADATA_RESERVE8,
	GFM_PROTO_METADATA_RESERVE9,
	GFM_PROTO_METADATA_RESERVE10,
	GFM_PROTO_METADATA_RESERVE11,
	GFM_PROTO_METADATA_RESERVE12,
	GFM_PROTO_METADATA_RESERVE13,
	GFM_PROTO_METADATA_RESERVE14,
	GFM_PROTO_METADATA_RESERVE15,

	/* gfs from client */

	GFM_PROTO_COMPOUND_BEGIN,		/* from gfsd, too */
	GFM_PROTO_COMPOUND_END,			/* from gfsd, too */
	GFM_PROTO_COMPOUND_ON_ERROR,		/* from gfsd, too */
	GFM_PROTO_PUT_FD,			/* from gfsd, too */
	GFM_PROTO_GET_FD,			/* from gfsd, too */
	GFM_PROTO_SAVE_FD,			/* from gfsd, too */
	GFM_PROTO_RESTORE_FD,			/* from gfsd, too */
	GFM_PROTO_BEQUEATH_FD,
	GFM_PROTO_INHERIT_FD,
	GFM_PROTO_CONTROL_OP_RESERVE9,
	GFM_PROTO_CONTROL_OP_RESERVE10,
	GFM_PROTO_CONTROL_OP_RESERVE11,
	GFM_PROTO_CONTROL_OP_RESERVE12,
	GFM_PROTO_CONTROL_OP_RESERVE13,
	GFM_PROTO_CONTROL_OP_RESERVE14,
	GFM_PROTO_CONTROL_OP_RESERVE15,

	GFM_PROTO_OPEN_ROOT,		/* from gfsd, too */
	GFM_PROTO_OPEN_PARENT,		/* from gfsd, too */
	GFM_PROTO_OPEN,			/* from gfsd, too */
	GFM_PROTO_CREATE,		/* from gfsd, too */
	GFM_PROTO_CLOSE,		/* from gfsd, too */
	GFM_PROTO_VERIFY_TYPE,
	GFM_PROTO_VERIFY_TYPE_NOT,
	GFM_PROTO_FD_MNG_OP_RESERVE7,
	GFM_PROTO_FD_MNG_OP_RESERVE8,
	GFM_PROTO_FD_MNG_OP_RESERVE9,
	GFM_PROTO_FD_MNG_OP_RESERVE10,
	GFM_PROTO_FD_MNG_OP_RESERVE11,
	GFM_PROTO_FD_MNG_OP_RESERVE12,
	GFM_PROTO_FD_MNG_OP_RESERVE13,
	GFM_PROTO_FD_MNG_OP_RESERVE14,
	GFM_PROTO_FD_MNG_OP_RESERVE15,

	GFM_PROTO_FSTAT,		/* from gfsd, too */
	GFM_PROTO_FUTIMES,		/* from gfsd, too */
	GFM_PROTO_FCHMOD,		/* from gfsd, too */
	GFM_PROTO_FCHOWN,		/* from gfsd, too */
	GFM_PROTO_CKSUM_GET,
	GFM_PROTO_CKSUM_SET,
	GFM_PROTO_SCHEDULE_FILE,
	GFM_PROTO_SCHEDULE_FILE_WITH_PROGRAM,
	GFM_PROTO_FD_OP_RESERVE8,
	GFM_PROTO_FD_OP_RESERVE9,
	GFM_PROTO_FD_OP_RESERVE10,
	GFM_PROTO_FD_OP_RESERVE11,
	GFM_PROTO_FD_OP_RESERVE12,
	GFM_PROTO_FD_OP_RESERVE13,
	GFM_PROTO_FD_OP_RESERVE14,
	GFM_PROTO_FD_OP_RESERVE15,

	GFM_PROTO_REMOVE,
	GFM_PROTO_RENAME,
	GFM_PROTO_FLINK,
	GFM_PROTO_MKDIR,
	GFM_PROTO_SYMLINK,
	GFM_PROTO_READLINK,
	GFM_PROTO_GETDIRPATH,
	GFM_PROTO_GETDIRENTS,
	GFM_PROTO_SEEK,
	GFM_PROTO_GETDIRENTSPLUS,
	GFM_PROTO_DIR_OP_RESERVE10,
	GFM_PROTO_DIR_OP_RESERVE11,
	GFM_PROTO_DIR_OP_RESERVE12,
	GFM_PROTO_DIR_OP_RESERVE13,
	GFM_PROTO_DIR_OP_RESERVE14,
	GFM_PROTO_DIR_OP_RESERVE15,

	/* gfs from gfsd */

	GFM_PROTO_REOPEN,
	GFM_PROTO_CLOSE_READ,
	GFM_PROTO_CLOSE_WRITE,
	GFM_PROTO_LOCK,
	GFM_PROTO_TRYLOCK,
	GFM_PROTO_UNLOCK,
	GFM_PROTO_LOCK_INFO,
	GFM_PROTO_SWITCH_BACK_CHANNEL,
	GFM_PROTO_FILE_OP_RESERVE8,
	GFM_PROTO_FILE_OP_RESERVE9,
	GFM_PROTO_FILE_OP_RESERVE10,
	GFM_PROTO_FILE_OP_RESERVE11,
	GFM_PROTO_FILE_OP_RESERVE12,
	GFM_PROTO_FILE_OP_RESERVE13,
	GFM_PROTO_FILE_OP_RESERVE14,
	GFM_PROTO_FILE_OP_RESERVE15,

	/* gfs_pio from client */

	GFM_PROTO_GLOB,
	GFM_PROTO_SCHEDULE,
	GFM_PROTO_PIO_OPEN,
	GFM_PROTO_PIO_SET_PATHS,
	GFM_PROTO_PIO_CLOSE,
	GFM_PROTO_PIO_VISIT,
	GFM_PROTO_PIO_OP_RESERVE6,
	GFM_PROTO_PIO_OP_RESERVE7,
	GFM_PROTO_PIO_OP_RESERVE8,
	GFM_PROTO_PIO_OP_RESERVE9,
	GFM_PROTO_PIO_OP_RESERVE10,
	GFM_PROTO_PIO_OP_RESERVE11,
	GFM_PROTO_PIO_OP_RESERVE12,
	GFM_PROTO_PIO_OP_RESERVE13,
	GFM_PROTO_PIO_OP_RESERVE14,
	GFM_PROTO_PIO_OP_RESERVE15,

	GFM_PROTO_PIO_MISC_RESERVE0,
	GFM_PROTO_PIO_MISC_RESERVE1,
	GFM_PROTO_PIO_MISC_RESERVE2,
	GFM_PROTO_PIO_MISC_RESERVE3,
	GFM_PROTO_PIO_MISC_RESERVE4,
	GFM_PROTO_PIO_MISC_RESERVE5,
	GFM_PROTO_PIO_MISC_RESERVE6,
	GFM_PROTO_PIO_MISC_RESERVE7,
	GFM_PROTO_PIO_MISC_RESERVE8,
	GFM_PROTO_PIO_MISC_RESERVE9,
	GFM_PROTO_PIO_MISC_RESERVE10,
	GFM_PROTO_PIO_MISC_RESERVE11,
	GFM_PROTO_PIO_MISC_RESERVE12,
	GFM_PROTO_PIO_MISC_RESERVE13,
	GFM_PROTO_PIO_MISC_RESERVE14,
	GFM_PROTO_PIO_MISC_RESERVE15,

	GFM_PROTO_HOSTNAME_SET,
	GFM_PROTO_SCHEDULE_HOST_DOMAIN,
	GFM_PROTO_STATFS,
	GFM_PROTO_MISC_RESERVE3,
	GFM_PROTO_MISC_RESERVE4,
	GFM_PROTO_MISC_RESERVE5,
	GFM_PROTO_MISC_RESERVE6,
	GFM_PROTO_MISC_RESERVE7,
	GFM_PROTO_MISC_RESERVE8,
	GFM_PROTO_MISC_RESERVE9,
	GFM_PROTO_MISC_RESERVE10,
	GFM_PROTO_MISC_RESERVE11,
	GFM_PROTO_MISC_RESERVE12,
	GFM_PROTO_MISC_RESERVE13,
	GFM_PROTO_MISC_RESERVE14,
	GFM_PROTO_MISC_RESERVE15,

	/* replica management from client */

	GFM_PROTO_REPLICA_LIST_BY_NAME,
	GFM_PROTO_REPLICA_LIST_BY_HOST,
	GFM_PROTO_REPLICA_REMOVE_BY_HOST,
	GFM_PROTO_REPLICA_REMOVE_BY_FILE,
	GFM_PROTO_REPLICA_OP_RESERVE4,
	GFM_PROTO_REPLICA_OP_RESERVE5,
	GFM_PROTO_REPLICA_OP_RESERVE6,
	GFM_PROTO_REPLICA_OP_RESERVE7,
	GFM_PROTO_REPLICA_OP_RESERVE8,
	GFM_PROTO_REPLICA_OP_RESERVE9,
	GFM_PROTO_REPLICA_OP_RESERVE10,
	GFM_PROTO_REPLICA_OP_RESERVE11,
	GFM_PROTO_REPLICA_OP_RESERVE12,
	GFM_PROTO_REPLICA_OP_RESERVE13,
	GFM_PROTO_REPLICA_OP_RESERVE14,
	GFM_PROTO_REPLICA_OP_RESERVE15,

	/* replica management from gfsd */

	GFM_PROTO_REPLICA_ADDING,
	GFM_PROTO_REPLICA_ADDED,
	GFM_PROTO_REPLICA_REMOVE,
	GFM_PROTO_REPLICA_ADD,
	GFM_PROTO_REPLICA_MNG_RESERVE4,
	GFM_PROTO_REPLICA_MNG_RESERVE5,
	GFM_PROTO_REPLICA_MNG_RESERVE6,
	GFM_PROTO_REPLICA_MNG_RESERVE7,
	GFM_PROTO_REPLICA_MNG_RESERVE8,
	GFM_PROTO_REPLICA_MNG_RESERVE9,
	GFM_PROTO_REPLICA_MNG_RESERVE10,
	GFM_PROTO_REPLICA_MNG_RESERVE11,
	GFM_PROTO_REPLICA_MNG_RESERVE12,
	GFM_PROTO_REPLICA_MNG_RESERVE13,
	GFM_PROTO_REPLICA_MNG_RESERVE14,
	GFM_PROTO_REPLICA_MNG_RESERVE15,

	/* job management */

	GFM_PROTO_PROCESS_ALLOC,
	GFM_PROTO_PROCESS_ALLOC_CHILD,
	GFM_PROTO_PROCESS_FREE,
	GFM_PROTO_PROCESS_SET,
	GFM_PROTO_PROCESS_RESERVE4,
	GFM_PROTO_PROCESS_RESERVE5,
	GFM_PROTO_PROCESS_RESERVE6,
	GFM_PROTO_PROCESS_RESERVE7,
	GFM_PROTO_PROCESS_RESERVE8,
	GFM_PROTO_PROCESS_RESERVE9,
	GFM_PROTO_PROCESS_RESERVE10,
	GFM_PROTO_PROCESS_RESERVE11,
	GFM_PROTO_PROCESS_RESERVE12,
	GFM_PROTO_PROCESS_RESERVE13,
	GFM_PROTO_PROCESS_RESERVE14,
	GFM_PROTO_PROCESS_RESERVE15,

	GFJ_PROTO_LOCK_REGISTER,
	GFJ_PROTO_UNLOCK_REGISTER,
	GFJ_PROTO_REGISTER,
	GFJ_PROTO_UNREGISTER,
	GFJ_PROTO_REGISTER_NODE,
	GFJ_PROTO_LIST,
	GFJ_PROTO_INFO,
	GFJ_PROTO_HOSTINFO,
	GFJ_PROTO_RESERVE8,
	GFJ_PROTO_RESERVE9,
	GFJ_PROTO_RESERVE10,
	GFJ_PROTO_RESERVE11,
	GFJ_PROTO_RESERVE12,
	GFJ_PROTO_RESERVE13,
	GFJ_PROTO_RESERVE14,
	GFJ_PROTO_RESERVE15,
};

#define GFM_PROTO_PROCESS_KEY_TYPE_SHAREDSECRET	1
#define GFM_PROTO_PROCESS_KEY_LEN_SHAREDSECRET	32

/* GFM_PROTO_CKSUM_GET flags */
#define	GFM_PROTO_CKSUM_GET_MAYBE_EXPIRED	0x00000001
#define	GFM_PROTO_CKSUM_GET_EXPIRED		0x00000002

/* GFM_PROTO_CKSUM_SET flags */
#define	GFM_PROTO_CKSUM_SET_FILE_MODIFIED	0x00000001

#define GFM_PROTO_CKSUM_MAXLEN	256

#define GFM_PROTO_MAX_DIRENT	10240

/* GFM_PROTO_SCHEDULE_FILE, GFM_PROTO_SCHEDULE_FILE_WITH_PROGRAM */
#define GFM_PROTO_SCHED_FLAG_HOST_AVAIL		1 /* always TRUE for now */
#define GFM_PROTO_SCHED_FLAG_LOADAVG_AVAIL	2 /* always TRUE for now */
#define GFM_PROTO_SCHED_FLAG_RTT_AVAIL		4 /* always FALSE for now */
#define GFM_PROTO_LOADAVG_FSCALE 		2048


#if 0 /* There isn't gfm_proto.c for now. */
extern char GFM_SERVICE_TAG[];
#else
#define GFM_SERVICE_TAG "gfarm-metadata"
#endif
