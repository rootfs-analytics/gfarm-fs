/*
 * $Id$
 */
struct inode;
struct host;

char *gfarm_server_fsngroup_find_replicainfo_by_inode(struct inode *);
void gfarm_server_fsngroup_replicate_file(
	struct inode *,	struct host *, char *);

