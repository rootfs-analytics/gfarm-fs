struct gfarm2fs_param {
	const char *mount_point;
	int foreground;
	int debug;
	double cache_timeout;
	int use_syslog;
	char *facility;
	char *loglevel;
	int ncopy;
	int copy_limit;
};

int gfarm2fs_getxattr(const char *, const char *, char *, size_t);
