/*
 * $Id$
 */

/*
 * << Journal File Header Format >>
 *
 *           |MAGIC(4)|VERSION(4)|0000...(4088)|
 *   offset  0        4          8          4096
 *
 * << Journal Record Format >>
 *
 *           |MAGIC(4)|SEQNUM(8)|OPE_ID(4)|DATA_LENGTH(4)=n|
 *   offset  0        4        12        16               20
 *
 *           |OBJECT(n)|CRC32(4)|
 *   offset 20        20+n    24+n
 */

#include <gfarm/gfarm_config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <pthread.h>
#include <assert.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gflog.h>

#include "crc32.h"
#include "iobuffer.h"
#include "gfp_xdr.h"
#include "io_fd.h"

#include "subr.h"
#include "thrsubr.h"
#include "journal_file.h"


struct journal_file;
struct journal_file_writer;

struct journal_file_reader {
	struct journal_file_reader *next;
	struct journal_file *file;
	off_t pos, la_pos;
	size_t cache_size, la_cache_size;
	struct gfp_xdr *xdr;
	int flags;
};

#define JOURNAL_FILE_READER_F_BLOCK_WRITER	0x1
#define JOURNAL_FILE_READER_F_WRAP		0x2
#define JOURNAL_FILE_READER_F_INVALID		0x4
#define JOURNAL_FILE_READER_F_DRAIN		0x8
#define JOURNAL_FILE_READER_IS_BLOCK_WRITER(r) \
	(((r)->flags & JOURNAL_FILE_READER_F_BLOCK_WRITER) != 0)
#define JOURNAL_FILE_READER_IS_WRAP(r) \
	(((r)->flags & JOURNAL_FILE_READER_F_WRAP) != 0)
#define JOURNAL_FILE_READER_IS_INVALID(r) \
	(((r)->flags & JOURNAL_FILE_READER_F_INVALID) != 0)
#define JOURNAL_FILE_READER_DRAINED(r) \
	(((r)->flags & JOURNAL_FILE_READER_F_DRAIN) != 0)

#define JOURNAL_RECORD_SIZE_MAX			(1024 * 1024)
#define JOURNAL_FILE_HEADER_SIZE		4096
#define JOURNAL_FILE_HEADER_MIN_SIZE		(JOURNAL_FILE_HEADER_SIZE + 128)
#define GFARM_JOURNAL_RECORD_HEADER_XDR_FMT	"cccclii"
#define JOURNAL_BUFSIZE				8192
#define JOURNAL_READ_AHEAD_SIZE			8192


struct journal_file_writer {
	struct journal_file *file;
	off_t pos;
	struct gfp_xdr *xdr;
};

struct journal_file {
	struct journal_file_reader reader_list;
	struct journal_file_writer writer;
	char *path;
	size_t size, max_size;
	off_t tail;
	int wait_until_nonempty;
	pthread_cond_t nonfull_cond, nonempty_cond, drain_cond;
	pthread_mutex_t mutex;
};

#define JOURNAL_READER_LIST_HEAD(j) (&(j)->reader_list)

#define FOREACH_JOURNAL_READER(x, j) \
	for (x = (j)->reader_list.next; x != JOURNAL_READER_LIST_HEAD(j); \
	    x = x->next)
#define FOREACH_JOURNAL_READER_SAFE(x, y, j) \
	for (x = (j)->reader_list.next, y = x->next \
	    ; x != JOURNAL_READER_LIST_HEAD(j); x = y, y = x->next)

#define JOURNAL_RECORD_SIZE(data_len) (journal_rec_header_size() + \
	sizeof(gfarm_uint32_t) + (data_len))

static const char JOURNAL_FILE_STR[] = "journal_file";

static const char *journal_operation_names[] = {
	NULL,

	"BEGIN",
	"END",

	"HOST_ADD",
	"HOST_MODIFY",
	"HOST_REMOVE",

	"USER_ADD",
	"USER_MODIFY",
	"USER_REMOVE",

	"GROUP_ADD",
	"GROUP_MODIFY",
	"GROUP_REMOVE",

	"INODE_ADD",
	"INODE_MODIFY",
	"INODE_GEN_MODIFY",
	"INODE_NLINK_MODIFY",
	"INODE_SIZE_MODIFY",
	"INODE_MODE_MODIFY",
	"INODE_USER_MODIFY",
	"INODE_GROUP_MODIFY",
	"INODE_ATIME_MODIFY",
	"INODE_MTIME_MODIFY",
	"INODE_CTIME_MODIFY",

	"INODE_CKSUM_ADD",
	"INODE_CKSUM_MODIFY",
	"INODE_CKSUM_REMOVE",

	"FILECOPY_ADD",
	"FILECOPY_REMOVE",

	"DEADFILECOPY_ADD",
	"DEADFILECOPY_REMOVE",

	"DIRENTRY_ADD",
	"DIRENTRY_REMOVE",

	"SYMLINK_ADD",
	"SYMLINK_REMOVE",

	"XATTR_ADD",
	"XATTR_MODIFY",
	"XATTR_REMOVE",
	"XATTR_REMOVEALL",

	"QUOTA_ADD",
	"QUOTA_MODIFY",
	"QUOTA_REMOVE",

	"MDHOST_ADD",
	"MDHOST_MODIFY",
	"MDHOST_REMOVE",
};

struct journal_file_writer *
journal_file_writer(struct journal_file *jf)
{
	return (&jf->writer);
}

off_t
journal_file_tail(struct journal_file *jf)
{
	return (jf->tail);
}

static void
journal_file_mutex_lock(struct journal_file *jf, const char *diag)
{
	gfarm_mutex_lock(&jf->mutex, diag, JOURNAL_FILE_STR);
}

static void
journal_file_mutex_unlock(struct journal_file *jf, const char *diag)
{
	gfarm_mutex_unlock(&jf->mutex, diag, JOURNAL_FILE_STR);
}

int
journal_file_is_waiting_until_nonempty(struct journal_file *jf)
{
	int r;
	static const char *diag = "journal_file_is_waiting_until_nonempty";

	journal_file_mutex_lock(jf, diag);
	r = jf->wait_until_nonempty;
	journal_file_mutex_unlock(jf, diag);
	return (r);
}

struct journal_file_reader *
journal_file_main_reader(struct journal_file *jf)
{
	return (jf->reader_list.next);
}

static int
journal_file_has_writer(struct journal_file *jf)
{
	return (jf->writer.xdr != NULL);
}

static void
journal_file_reader_set_flag(struct journal_file_reader *reader, int f,
	int val)
{
	if (val)
		reader->flags |= f;
	else
		reader->flags &= ~f;
}

static void
journal_file_add_reader(struct journal_file *jf,
	struct journal_file_reader *reader)
{
	struct journal_file_reader *cur, *last = NULL;

	FOREACH_JOURNAL_READER(cur, jf)
		last = cur;
	if (last == NULL)
		last = JOURNAL_READER_LIST_HEAD(jf);
	last->next = reader;
	reader->next = JOURNAL_READER_LIST_HEAD(jf);
}

static void
journal_file_reader_invalidate(struct journal_file_reader *reader)
{
	journal_file_reader_set_flag(reader,
	    JOURNAL_FILE_READER_F_INVALID, 1);
}

void
journal_file_reader_disable_block_writer(struct journal_file_reader *reader)
{
	journal_file_reader_set_flag(reader,
	    JOURNAL_FILE_READER_F_BLOCK_WRITER, 0);
}

static void
journal_file_reader_free(struct journal_file_reader *reader)
{
	if (reader == NULL)
		return;
	journal_file_reader_close(reader);
	free(reader);
}

struct gfp_xdr *
journal_file_reader_xdr(struct journal_file_reader *reader)
{
	return (reader->xdr);
}

static off_t
journal_file_reader_cache_pos_unlocked(struct journal_file_reader *reader)
{
	return (reader->pos - reader->cache_size);
}

off_t
journal_file_reader_cache_pos(struct journal_file_reader *reader)
{
	struct journal_file *jf = reader->file;
	static const char *diag = "journal_file_reader_cache_pos";
	off_t p;

	journal_file_mutex_lock(jf, diag);
	p = journal_file_reader_cache_pos_unlocked(reader);
	journal_file_mutex_unlock(jf, diag);
	return (p);
}

void
journal_file_reader_commit_pos_unlocked(struct journal_file_reader *reader)
{
	struct journal_file *jf = reader->file;
	static const char *diag = "journal_file_reader_commit_pos_unlocked";

	reader->pos = reader->la_pos;
	reader->cache_size = reader->la_cache_size;
	gfarm_cond_signal(&jf->nonfull_cond, diag, JOURNAL_FILE_STR);
}

void
journal_file_reader_commit_pos(struct journal_file_reader *reader)
{
	struct journal_file *jf = reader->file;
	static const char *diag = "journal_file_reader_commit_pos";

	journal_file_mutex_lock(jf, diag);
	journal_file_reader_commit_pos_unlocked(reader);
	journal_file_mutex_unlock(jf, diag);
}

int
journal_file_reader_is_invalid(struct journal_file_reader *reader)
{
	return (JOURNAL_FILE_READER_IS_INVALID(reader));
}

static void
journal_file_writer_set(struct journal_file *jf, struct gfp_xdr *xdr,
	off_t pos, struct journal_file_writer *writer)
{
	writer->file = jf;
	writer->xdr = xdr;
	writer->pos = pos;
}

struct gfp_xdr *
journal_file_writer_xdr(struct journal_file_writer *writer)
{
	return (writer->xdr);
}

off_t
journal_file_writer_pos(struct journal_file_writer *writer)
{
	return (writer->pos);
}

static size_t
journal_rec_header_size(void)
{
	static size_t header_size = 0;

	if (header_size == 0) {
		gfarm_error_t e;
		if ((e = gfp_xdr_send_size_add(&header_size,
		    GFARM_JOURNAL_RECORD_HEADER_XDR_FMT,
		    0, 0, 0, 0, 0, 0, 0)) != GFARM_ERR_NO_ERROR) {
			gflog_fatal(GFARM_MSG_1002873,
			    "journal_rec_header_size : %s",
			    gfarm_error_string(e)); /* exit */
		}
	}
	return (header_size);
}

static int
jounal_read_fully(int fd, void *buf, size_t sz, off_t *posp, int *eofp)
{
	size_t rsz = sz;
	ssize_t ssz;

	*eofp = 0;
	while (rsz > 0) {
		if ((ssz = read(fd, buf, rsz)) < 0)
			return (ssz);
		if (ssz == 0) {
			*eofp = 1;
			sz = sz - rsz;
			break;
		}
		rsz -= ssz;
		if (posp)
			*posp += ssz;
	}
	return (sz);
}

static int
journal_read_uint32(int fd, gfarm_uint32_t *np, void *data, off_t *posp)
{
	ssize_t ssz;
	int eof;

	if ((ssz = jounal_read_fully(fd, np, sizeof(*np), posp,
	    &eof)) < 0)
		return (ssz);
	if (eof)
		return (0);
	if (data)
		memcpy(data, np, sizeof(*np));
	*np = ntohl(*np);
	return (sizeof(*np));
}

static int
journal_read_uint64(int fd, gfarm_uint64_t *np, void *data, off_t *posp)
{
	ssize_t ssz;
	gfarm_uint32_t n1, n2;

	if ((ssz = journal_read_uint32(fd, &n1, data, posp)) <= 0)
		return (ssz);
	if ((ssz = journal_read_uint32(fd, &n2,
	    data ? data + sizeof(n1) : NULL, posp)) <= 0)
		return (ssz);
	*np = ((gfarm_uint64_t)n1 << 32) | n2;
	return (sizeof(*np));
}

static gfarm_uint32_t
journal_deserialize_uint32(unsigned char *d)
{
	return (d[0] << 24 | d[1] << 16 | d[2] << 8 | d[3]);
}

static gfarm_uint64_t
journal_deserialize_uint64(unsigned char *d)
{
	gfarm_uint32_t n1, n2;
	n1 = journal_deserialize_uint32(d);
	n2 = journal_deserialize_uint32(d + 4);
	return ((((gfarm_uint64_t)n1) << 32) | n2);
}

static gfarm_error_t
journal_write_zeros(int fd, size_t len)
{
	char buf[JOURNAL_BUFSIZE];
	size_t wlen;
	ssize_t ssz;

	memset(buf, 0, JOURNAL_BUFSIZE);
	errno = 0;
	while (len > 0) {
		wlen = len > sizeof(buf) ? sizeof(buf) : len;
		if ((ssz = write(fd, buf, wlen)) < 0)
			return (gfarm_errno_to_error(errno));
		len -= ssz;
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_file_writer_fill(struct journal_file_writer *writer)
{
	ssize_t len;
	struct journal_file *jf = writer->file;

	gfp_xdr_purge_all(writer->xdr);
	len = jf->max_size - writer->pos;
	assert(len >= 0);
	return (journal_write_zeros(gfp_xdr_fd(writer->xdr), len));
}

static int
journal_write_fully(int fd, void *data, size_t length, off_t *posp)
{
	size_t rlen = length;
	ssize_t ssz;

	errno = 0;
	while (rlen > 0) {
		if ((ssz = write(fd, data, rlen)) < 0)
			return (ssz);
		if (ssz == 0)
			return (length - rlen);
		rlen -= ssz;
		data += ssz;
		*posp += ssz;
	}
	return (length);
}

static gfarm_error_t
journal_seek(int fd, size_t file_size, gfarm_uint64_t *seqnump,
	enum journal_operation *opep, off_t *posp, off_t *next_posp)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	ssize_t ssz;
	gfarm_uint32_t crc, crc1;
	off_t pos, pos1;
	int eof, found = 0;
	char *rec = NULL;
	char magic[GFARM_JOURNAL_MAGIC_SIZE];
	gfarm_uint64_t seqnum = 0;
	gfarm_uint32_t ope;
	gfarm_uint32_t len;
	char buf[JOURNAL_BUFSIZE], *raw;

	*posp = *next_posp = -1;
	errno = 0;

	pos = lseek(fd, 0, SEEK_CUR);

	for (;;) {
		crc = 0;
		if (jounal_read_fully(fd, magic,
		    GFARM_JOURNAL_MAGIC_SIZE, &pos, &eof) < 0)
			return (gfarm_errno_to_error(errno));
		if (eof)
			break;
		raw = buf;
		memcpy(raw, magic, GFARM_JOURNAL_MAGIC_SIZE);
		if (strncmp(magic, GFARM_JOURNAL_RECORD_MAGIC,
			GFARM_JOURNAL_MAGIC_SIZE) != 0) {
			pos = lseek(fd, 1 - GFARM_JOURNAL_MAGIC_SIZE,
				SEEK_CUR);
			if (pos < 0)
				return (gfarm_errno_to_error(errno));
			continue;
		}
		if ((pos1 = lseek(fd, 0, SEEK_CUR)) < 0)
			return (gfarm_errno_to_error(errno));
		raw += GFARM_JOURNAL_MAGIC_SIZE;
		pos1 -= GFARM_JOURNAL_MAGIC_SIZE;
		if ((ssz = journal_read_uint64(fd, &seqnum,
		    raw, &pos)) < 0)
			return (gfarm_errno_to_error(errno));
		if (ssz == 0)
			goto next;
		raw += sizeof(gfarm_uint64_t);
		if ((ssz = journal_read_uint32(fd, &ope,
		    raw, &pos)) < 0)
			return (gfarm_errno_to_error(errno));
		if (ssz == 0)
			goto next;
		raw += sizeof(gfarm_uint32_t);
		*opep = ope;
		if ((ssz = journal_read_uint32(fd, &len,
		    raw, &pos)) < 0)
			return (gfarm_errno_to_error(errno));
		if (ssz == 0)
			goto next;
		raw += sizeof(gfarm_uint32_t);
		if (pos + len + sizeof(crc) > file_size ||
		    len > JOURNAL_RECORD_SIZE_MAX)
			goto next;
		crc = gfarm_crc32(0, buf, journal_rec_header_size());
		GFARM_MALLOC_ARRAY(rec, len);
		if (rec == NULL)
			return (GFARM_ERR_NO_MEMORY);
		if (jounal_read_fully(fd, rec, len, &pos, &eof) < 0) {
			e = gfarm_errno_to_error(errno);
			goto next;
		}
		if (eof)
			goto next;
		if ((ssz = journal_read_uint32(fd, &crc1, NULL, &pos)) < 0) {
			e = gfarm_errno_to_error(errno);
			goto next;
		}
		if (ssz == 0)
			goto next;
		crc = gfarm_crc32(crc, rec, len);
		if (crc != crc1)
			goto next;
		*seqnump = seqnum;
		*posp = pos1;
		*next_posp = pos;
		found = 1;
next:
		if (rec) {
			free(rec);
			rec = NULL;
		}
		if (found)
			break;
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		pos = lseek(fd, pos1 - pos + 1, SEEK_CUR);
		if (pos < 0)
			return (gfarm_errno_to_error(errno));
	}

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_file_reader_rewind(struct journal_file_reader *reader)
{
	gfarm_error_t e;

	errno = 0;
	reader->la_pos = lseek(gfp_xdr_fd(reader->xdr),
		JOURNAL_FILE_HEADER_SIZE, SEEK_SET);
	if (reader->la_pos == -1) {
		e = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1002874,
		    "lseek : %s", gfarm_error_string(e));
		return (e);
	}
	gfp_xdr_recvbuffer_clear_read_eof(reader->xdr);
	journal_file_reader_set_flag(reader,
	    JOURNAL_FILE_READER_F_WRAP, 0);
	return (GFARM_ERR_NO_ERROR);
}

static int
journal_file_reader_needs_wait(struct journal_file_reader *reader,
	off_t wpos, size_t rec_len, size_t max_size)
{
	off_t cpos = journal_file_reader_cache_pos_unlocked(reader);

	/*
	 * (wpos < cpos && wpos + rec_len > cpos)
	 *
	 *           w    c          max_size
	 *     +--+--+----+----------+
	 *     |FH|  |rec_len|
	 *
	 * (cpos < rec_len + JOURNAL_FILE_HEADER_SIZE &&
	 *  wpos + rec_len > max_size))
	 *
	 *             c          w  max_size
	 *     +--+----+----------+--+
	 *     |FH|rec_len|       |rec_len|
	 */
	return ((wpos < cpos && wpos + rec_len > cpos) ||
		(cpos < rec_len + JOURNAL_FILE_HEADER_SIZE &&
		wpos + rec_len > max_size));
}

static gfarm_error_t
journal_file_reader_check_pos(struct journal_file_reader *reader,
	off_t wpos, size_t rec_len, size_t max_size)
{
	struct journal_file *jf = reader->file;
	static const char *diag = "journal_file_reader_adjust_pos";
	off_t rpos, npos;

	if (JOURNAL_FILE_READER_IS_INVALID(reader))
		return (GFARM_ERR_NO_ERROR);
	if (JOURNAL_FILE_READER_IS_BLOCK_WRITER(reader)) {
		while (journal_file_reader_needs_wait(reader, wpos,
		    rec_len, max_size)) {
#ifdef DEBUG_JOURNAL
			gflog_info(GFARM_MSG_1002875,
			    "wait jounal_file.nonfull_cond");
#endif
			gfarm_cond_wait(&jf->nonfull_cond, &jf->mutex,
			    diag, JOURNAL_FILE_STR);
		}
		return (GFARM_ERR_NO_ERROR);
	}
	rpos = journal_file_reader_cache_pos_unlocked(reader);
	npos = wpos + rec_len;
	if (npos < max_size) {
		/* 0        w   r   n       max_size
		 * +--+-----+---+---+-------+
		 * |FH|     |rec_len|
		 *
		 */
		if (rpos < npos && rpos >= wpos &&
		    (rpos != wpos || JOURNAL_FILE_READER_IS_WRAP(reader)))
			journal_file_reader_invalidate(reader);
	} else {
		/* 0       r  n         w   max_size
		 * +--+----+--+---------+---+
		 * |FH|rec_len|         |rec_len|
		 *
		 * 0          n         w r max_size
		 * +--+-------+---------+-+-+
		 * |FH|rec_len|         |rec_len|
		 */
		if (rpos < rec_len + JOURNAL_FILE_HEADER_SIZE ||
		    (rpos >= wpos &&
		    (rpos != wpos || !JOURNAL_FILE_READER_IS_WRAP(reader))))
			journal_file_reader_invalidate(reader);
	}
	if (JOURNAL_FILE_READER_IS_INVALID(reader)) {
		gflog_debug(GFARM_MSG_1002876,
		    "invalidated journal_file_reader : rec_len=%lu, "
		    "rpos=%lu, wpos=%lu",
		    (unsigned long)rec_len, (unsigned long)rpos,
		    (unsigned long)wpos);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_file_check_pos(struct journal_file *jf, size_t rec_len)
{
	gfarm_error_t e;
	off_t pos, wpos;
	struct journal_file_writer *writer = &jf->writer;
	struct journal_file_reader *reader;

	if (rec_len + JOURNAL_FILE_HEADER_SIZE > jf->max_size) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_error(GFARM_MSG_1002877,
		    "rec_len(%lu) + fh(%lu) > max_size(%lu) : %s",
		    (unsigned long)rec_len,
		    (unsigned long)JOURNAL_FILE_HEADER_SIZE,
		    (unsigned long)jf->max_size,
		    gfarm_error_string(e));
		return (e);
	}
	wpos = writer->pos;
	FOREACH_JOURNAL_READER(reader, jf) {
		if ((e = journal_file_reader_check_pos(reader, wpos, rec_len,
		    jf->max_size)) != GFARM_ERR_NO_ERROR)
			return (e);
	}
	if (wpos + rec_len > jf->max_size) {
		jf->tail = writer->pos;
		if ((e = journal_file_writer_fill(writer))
		    != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1002878,
			    "journal_file_writer_fill : %s",
			    gfarm_error_string(e));
			return (e);
		}
		jf->size = jf->max_size;
		pos = lseek(gfp_xdr_fd(writer->xdr), JOURNAL_FILE_HEADER_SIZE,
			SEEK_SET);
		if (pos < 0) {
			e = gfarm_errno_to_error(e);
			gflog_error(GFARM_MSG_1002879,
			    "lseek : %s",
			    gfarm_error_string(e));
			return (e);
		}
		writer->pos = pos;
		FOREACH_JOURNAL_READER(reader, jf)
			journal_file_reader_set_flag(reader,
			    JOURNAL_FILE_READER_F_WRAP, 1);
	}
	FOREACH_JOURNAL_READER(reader, jf) {
		if (reader->xdr)
			gfp_xdr_recvbuffer_clear_read_eof(reader->xdr);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_read_file_header(int fd)
{
	gfarm_error_t e;
	int no;
	off_t pos = 0;
	int eof;
	char magic[GFARM_JOURNAL_MAGIC_SIZE];
	gfarm_uint32_t version;

	errno = 0;
	if (jounal_read_fully(fd, magic, GFARM_JOURNAL_MAGIC_SIZE,
	    &pos, &eof) < 0) {
		no = GFARM_MSG_1002880;
		e = gfarm_errno_to_error(errno);
	} else if (strncmp(magic, GFARM_JOURNAL_FILE_MAGIC,
	    GFARM_JOURNAL_MAGIC_SIZE) != 0) {
		no = GFARM_MSG_1002881;
		e = GFARM_ERR_INTERNAL_ERROR;
	} else if (journal_read_uint32(fd, &version, NULL, &pos) <= 0) {
		no = GFARM_MSG_1002882;
		e = GFARM_ERR_INTERNAL_ERROR;
	} else if (version != GFARM_JOURNAL_VERSION) {
		no = GFARM_MSG_1002883;
		e = GFARM_ERR_INTERNAL_ERROR;
	} else if (lseek(fd, JOURNAL_FILE_HEADER_SIZE, SEEK_SET) < 0) {
		no = GFARM_MSG_1002884;
		e = gfarm_errno_to_error(errno);
	} else
		return (GFARM_ERR_NO_ERROR);

	gflog_error(no, "invalid journal file format");
	return (e);
}

static gfarm_error_t
journal_find_rw_pos(int rfd, int wfd, size_t file_size,
	gfarm_uint64_t cur_seqnum, off_t *rposp, off_t *wposp,
	off_t *tailp, int *wrapp)
{
	gfarm_error_t e;
	off_t pos, pos0 = 0, pos1, pos2, tail = 0;
	off_t min_seqnum_pos, max_seqnum_next_pos;
	enum journal_operation ope = 0;
	gfarm_uint64_t min_seqnum, max_seqnum, seqnum = 0, seqnum0 = 0;
	int stored_all = 0;

	min_seqnum = GFARM_UINT64_MAX;
	max_seqnum = 0;
	min_seqnum_pos = max_seqnum_next_pos = -1;

	if ((e = journal_read_file_header(rfd)) != GFARM_ERR_NO_ERROR)
		return (e);

	errno = 0;
	for (;;) {
		if ((e = journal_seek(rfd, file_size,
		    &seqnum, &ope, &pos1, &pos2)) != GFARM_ERR_NO_ERROR) {
			return (e);
		}
		if (pos1 == -1)
			break;
		pos = pos1;
		if (ope == GFM_JOURNAL_BEGIN) {
			pos0 = pos1;
			seqnum0 = seqnum;
		} else if (ope == GFM_JOURNAL_END &&
		    max_seqnum < seqnum && cur_seqnum <= seqnum) {
			max_seqnum = seqnum;
			max_seqnum_next_pos = pos2;
			if (cur_seqnum < seqnum0 && min_seqnum > seqnum0) {
				min_seqnum = seqnum0;
				min_seqnum_pos = pos0;
			}
		}
		tail = pos2;
		if (lseek(rfd, pos2, SEEK_SET) < 0)
			return (gfarm_errno_to_error(errno));
	}

	if (min_seqnum_pos < 0 && max_seqnum_next_pos > 0) {
		min_seqnum_pos = max_seqnum_next_pos;
		stored_all = 1;
	}

	if (min_seqnum_pos < 0) {
		*rposp = JOURNAL_FILE_HEADER_SIZE;
		if (wposp)
			*wposp = *rposp;
		if (tailp)
			*tailp = *rposp;
		if (wrapp)
			*wrapp = 0;
	} else {
		*rposp = min_seqnum_pos;
		if (wposp)
			*wposp = max_seqnum_next_pos;
		if (tailp)
			*tailp = tail;
		if (wrapp)
			*wrapp = min_seqnum_pos >= max_seqnum_next_pos &&
			    stored_all == 0;
	}
	if (lseek(rfd, *rposp, SEEK_SET) < 0)
		return (gfarm_errno_to_error(errno));
	if (wfd >= 0 && wposp && lseek(wfd, *wposp, SEEK_SET) < 0)
		return (gfarm_errno_to_error(errno));

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_close_fd_op(void *cookie, int fd)
{
	return (close(fd) == -1 ? gfarm_errno_to_error(errno) :
	    GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_export_credential_fd_op(void *cookie)
{
	/* it's already exported, or no way to export it. */
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_delete_credential_fd_op(void *cookie, int sighandler)
{
	return (GFARM_ERR_NO_ERROR);
}

static char *
journal_env_for_credential_fd_op(void *cookie)
{
	return (NULL);
}

static int
journal_nonblocking_read_err_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	errno = EPERM;
	gfarm_iobuffer_set_error(b, gfarm_errno_to_error(errno));
	return (-1);
}

static int
journal_nonblocking_write_err_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	errno = EPERM;
	gfarm_iobuffer_set_error(b, gfarm_errno_to_error(errno));
	return (-1);
}

static int
journal_blocking_read_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	gfarm_error_t e;
	int eof;
	size_t rlen;
	ssize_t ssz;
	struct journal_file_reader *reader = cookie;
	struct journal_file *jf = reader->file;
	struct journal_file_writer *writer = &jf->writer;
	off_t wpos = writer->pos, rpos = reader->la_pos;

	assert(rpos >= 0 && wpos >= 0);

	if (rpos == wpos && !JOURNAL_FILE_READER_IS_WRAP(reader))
		return (0);
	if (rpos < wpos)
		rlen = rpos + length <= wpos ? length : wpos - rpos;
	else if (rpos < jf->tail)
		rlen = rpos + length <= jf->tail ? length : jf->tail - rpos;
	else {
		assert(rpos == jf->tail);
		if ((e = journal_file_reader_rewind(reader))
		    != GFARM_ERR_NO_ERROR) {
			gfarm_iobuffer_set_error(b, e);
			return (-1);
		}
		rlen = length <= wpos - JOURNAL_FILE_HEADER_SIZE ?
		    length : wpos - JOURNAL_FILE_HEADER_SIZE;
	}
	errno = 0;
	ssz = jounal_read_fully(fd, data, rlen, &reader->la_pos, &eof);
	if (ssz < 0) {
		gfarm_iobuffer_set_error(b, gfarm_errno_to_error(errno));
		return (ssz);
	}
	if (eof)
		gfarm_iobuffer_set_read_eof(b);
	reader->la_cache_size += ssz;
	return (ssz);
}

static int
journal_blocking_write_op(struct gfarm_iobuffer *b,
	void *cookie, int fd, void *data, int length)
{
	ssize_t ssz;
	struct journal_file_writer *writer = cookie;
	struct journal_file *jf = writer->file;

	errno = 0;
	if ((ssz = journal_write_fully(fd, data, length, &writer->pos)) < 0)
		gfarm_iobuffer_set_error(b, gfarm_errno_to_error(errno));
	if (jf->size < writer->pos)
		jf->size = jf->tail = writer->pos;
	return (ssz);
}

static int
journal_write_uint32(int fd, gfarm_uint32_t n, off_t *posp)
{
	n = htonl(n);
	return (journal_write_fully(fd, &n, sizeof(n), posp));
}

static gfarm_error_t
journal_write_file_header(int fd)
{
	gfarm_error_t e;
	int no;
	char magic[GFARM_JOURNAL_MAGIC_SIZE];
	ssize_t ssz;
	off_t pos = 0;

	memcpy(magic, GFARM_JOURNAL_FILE_MAGIC, sizeof(magic));

	if ((ssz = journal_write_fully(fd, magic, sizeof(magic), &pos)) < 0) {
		no = GFARM_MSG_1002885;
		e = gfarm_errno_to_error(errno);
	} else if ((ssz = journal_write_uint32(fd, GFARM_JOURNAL_VERSION,
	    &pos)) < 0) {
		no = GFARM_MSG_1002886;
		e = gfarm_errno_to_error(errno);
	} else if ((e = journal_write_zeros(fd, JOURNAL_FILE_HEADER_SIZE -
	    pos)) != GFARM_ERR_NO_ERROR) {
		no = GFARM_MSG_1002887;
	} else {
		errno = 0;
#ifdef HAVE_FDATASYNC
		if (fdatasync(fd) == -1)
#else
		if (fsync(fd) == -1)
#endif
			return (gfarm_errno_to_error(errno));
		return (GFARM_ERR_NO_ERROR);
	}
	gflog_error(no,
	    "failed to write journal file header : %s",
	    gfarm_error_string(e));
	return (e);
}

static struct gfp_iobuffer_ops journal_iobuffer_ops = {
	journal_close_fd_op,
	journal_export_credential_fd_op,
	journal_delete_credential_fd_op,
	journal_env_for_credential_fd_op,
	journal_nonblocking_read_err_op,
	journal_nonblocking_write_err_op,
	journal_blocking_read_op,
	journal_blocking_read_op,
	journal_blocking_write_op
};

static gfarm_error_t
journal_file_reader_init(struct journal_file *jf, int fd,
	off_t pos, int block_writer, int wrap,
	struct journal_file_reader *reader)
{
	gfarm_error_t e;
	struct gfp_xdr *xdr;

	if (lseek(fd, pos, SEEK_SET) == -1) {
		e = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1002888,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	if ((e = gfp_xdr_new_recv_only(&journal_iobuffer_ops,
	    reader, fd, &xdr))) {
		free(reader);
		gflog_error(GFARM_MSG_1002889,
		    "%s", gfarm_error_string(e));
		return (e);
	}

	reader->file = jf;
	reader->pos = pos;
	reader->la_pos = pos;
	reader->cache_size = 0;
	reader->la_cache_size = 0;
	reader->xdr = xdr;
	reader->flags = 0;
	journal_file_reader_set_flag(reader,
	    JOURNAL_FILE_READER_F_BLOCK_WRITER, block_writer);
	journal_file_reader_set_flag(reader,
	    JOURNAL_FILE_READER_F_WRAP, wrap);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_file_reader_new(struct journal_file *jf, int fd,
	off_t pos, int block_writer, int wrap,
	struct journal_file_reader **readerp)
{
	gfarm_error_t e;
	struct journal_file_reader *reader;

	GFARM_MALLOC(reader);
	if (reader == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_1002890,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	if ((e = journal_file_reader_init(jf, fd, pos, block_writer, wrap,
	    reader)) != GFARM_ERR_NO_ERROR) {
		free(reader);
		return (e);
	}
	journal_file_add_reader(jf, reader);
	*readerp = reader;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
journal_file_open(const char *path, size_t max_size,
	gfarm_uint64_t cur_seqnum, struct journal_file **jfp, int flags)
{
	gfarm_error_t e;
	struct stat st;
	int r, reader_fd = -1, writer_fd = -1, wrap = 0;
	size_t size = 0;
	off_t reader_pos = 0, writer_pos = 0, tail = 0;
	struct journal_file *jf;
	struct journal_file_reader *reader = NULL;
	struct gfp_xdr *writer_xdr = NULL;
	static const char *diag = "journal_file_open";

	GFARM_MALLOC(jf);
	if (jf == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_1002891,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	memset(jf, 0, sizeof(*jf));
	errno = 0;
	r = stat(path, &st);
	if (r == -1 && errno != ENOENT) {
		e = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1002892,
		    "stat : %s",
		    gfarm_error_string(e));
		goto error;
	} else if (r == 0) {
		size = st.st_size;
		if (size < JOURNAL_FILE_HEADER_SIZE) {
			e = GFARM_ERR_INTERNAL_ERROR;
			gflog_warning(GFARM_MSG_1002893,
			    "invalid journal file size : %lu",
			    (unsigned long)size);
			r = unlink(path);
			if (r == -1) {
				e = gfarm_errno_to_error(errno);
				gflog_warning(GFARM_MSG_1002894,
				    "failed to unlink %s: %s", path,
				    gfarm_error_string(e));
				goto error;
			}
		}
	}

	if (size > 0 && max_size > 0 && size > max_size) {
		e = GFARM_ERR_FILE_TOO_LARGE;
		gflog_error(GFARM_MSG_1002895,
		    "journal file is larger than journal_max_size."
		    " path : %s, size : %lu > %lu", path,
		    (unsigned long)size, (unsigned long)max_size);
		return (e);
	}
	if ((flags & GFARM_JOURNAL_RDWR) != 0 &&
	    max_size < JOURNAL_FILE_HEADER_MIN_SIZE) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_error(GFARM_MSG_1002896,
		    "journal_max_size must be larger than %d. current=%lu",
		    JOURNAL_FILE_HEADER_MIN_SIZE, (unsigned long)max_size);
		return (e);
	}

	jf->size = size;
	jf->max_size = (flags & GFARM_JOURNAL_RDONLY) != 0 ? size : max_size;

	if ((flags & GFARM_JOURNAL_RDWR) != 0) {
		writer_fd = open(path, O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR);
		if (writer_fd < 0) {
			e = gfarm_errno_to_error(errno);
			gflog_error(GFARM_MSG_1002897,
			    "open for write: %s",
			    gfarm_error_string(e));
			goto error;
		}
		fsync(writer_fd);
	}
	reader_fd = open(path, O_RDONLY);
	if (reader_fd < 0) {
		e = gfarm_errno_to_error(errno);
		gflog_error(GFARM_MSG_1002898,
		    "open for read: %s",
		    gfarm_error_string(e));
		goto error;
	}
	if (size > 0) {
		if ((e = journal_find_rw_pos(reader_fd, writer_fd,
		    size, cur_seqnum, &reader_pos, &writer_pos,
		    &tail, &wrap)) != GFARM_ERR_NO_ERROR)
			goto error;
	} else {
		if (writer_fd >= 0) {
			if ((e = journal_write_file_header(writer_fd))
			    != GFARM_ERR_NO_ERROR)
				goto error;
		}
		writer_pos = JOURNAL_FILE_HEADER_SIZE;
		errno = 0;
		if (lseek(reader_fd, writer_pos, SEEK_SET) < 0) {
			e = gfarm_errno_to_error(errno);
			goto error;
		}
		tail = reader_pos = JOURNAL_FILE_HEADER_SIZE;
	}
	jf->path = strdup_log(path, "journal_file_open");
	if (jf->path == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto error;
	}
	jf->tail = tail;
	if ((flags & GFARM_JOURNAL_RDWR) != 0) {
		if ((e = gfp_xdr_new_send_only(&journal_iobuffer_ops,
		    &jf->writer, writer_fd, &writer_xdr)))
			goto error;
		journal_file_writer_set(jf, writer_xdr, writer_pos,
			&jf->writer);
	} else {
		journal_file_writer_set(jf, NULL, writer_pos,
			&jf->writer);
	}

	jf->reader_list.next = JOURNAL_READER_LIST_HEAD(jf);
	if ((e = journal_file_reader_new(jf, reader_fd, reader_pos, 1,
	    wrap, &reader)) != GFARM_ERR_NO_ERROR)
		goto error;

	gfarm_cond_init(&jf->nonfull_cond, diag, JOURNAL_FILE_STR);
	gfarm_cond_init(&jf->nonempty_cond, diag, JOURNAL_FILE_STR);
	gfarm_cond_init(&jf->drain_cond, diag, JOURNAL_FILE_STR);
	gfarm_mutex_init(&jf->mutex, diag, JOURNAL_FILE_STR);
	jf->wait_until_nonempty = 0;
	*jfp = jf;

	return (GFARM_ERR_NO_ERROR);
error:
	if (reader_fd > 0)
		close(reader_fd);
	if (writer_xdr)
		gfp_xdr_free(writer_xdr);
	else if (writer_fd > 0)
		close(writer_fd);
	free(jf);
	journal_file_reader_free(reader);
	*jfp = NULL;
	return (e);
}

void
journal_file_reader_close(struct journal_file_reader *reader)
{
	journal_file_reader_set_flag(reader, JOURNAL_FILE_READER_F_DRAIN, 1);
	if (reader->xdr) {
		gfp_xdr_free(reader->xdr);
		reader->xdr = NULL;
	}
	journal_file_reader_invalidate(reader);
}

gfarm_error_t
journal_file_reader_reopen(struct journal_file *jf,
	struct journal_file_reader **readerp,
	gfarm_uint64_t seqnum)
{
	gfarm_error_t e;
	int fd;
	off_t rpos;
	int wrap;
	static const char *diag = "journal_file_reader_reopen";

	if ((fd = open(jf->path, O_RDONLY)) == -1)
		return (gfarm_errno_to_error(errno));

	journal_file_mutex_lock(jf, diag);
	assert(*readerp == NULL || (*readerp)->xdr == NULL);
	if ((e = journal_find_rw_pos(fd, -1, jf->tail, seqnum, &rpos,
	    NULL, NULL, &wrap)) != GFARM_ERR_NO_ERROR)
		;
	else if (*readerp)
		e = journal_file_reader_init(jf, fd, rpos, 0, wrap,
		    *readerp);
	else
		e = journal_file_reader_new(jf, fd, rpos, 0, wrap, readerp);
	journal_file_mutex_unlock(jf, diag);
	return (e);
}

void
journal_file_wait_until_empty(struct journal_file *jf)
{
	struct journal_file_writer *writer = &jf->writer;
	struct journal_file_reader *reader = journal_file_main_reader(jf);
	static const char *diag = "journal_file_wait_until_empty";
#ifdef DEBUG_JOURNAL
	gflog_debug(GFARM_MSG_1002899,
	    "wait until applying all rest journal records");
#endif
	journal_file_mutex_lock(jf, diag);
	while (writer->pos != reader->pos)
		gfarm_cond_wait(&jf->nonfull_cond, &jf->mutex, diag,
		    JOURNAL_FILE_STR);
	journal_file_mutex_unlock(jf, diag);
#ifdef DEBUG_JOURNAL
	gflog_debug(GFARM_MSG_1002900, "end applying journal records");
#endif
}

void
journal_file_close(struct journal_file *jf)
{
	struct journal_file_reader *reader, *reader2;
	static const char *diag = "journal_file_close";

	if (jf == NULL)
		return;
	journal_file_mutex_lock(jf, diag);
	FOREACH_JOURNAL_READER_SAFE(reader, reader2, jf)
		journal_file_reader_free(reader);
	jf->reader_list.next = JOURNAL_READER_LIST_HEAD(jf);
	if (jf->writer.xdr)
		gfp_xdr_free(jf->writer.xdr);
	jf->writer.xdr = NULL;
	free(jf->path);
	jf->path = NULL;
	journal_file_mutex_unlock(jf, diag);
}

int
journal_file_is_closed(struct journal_file *jf)
{
	return (jf->path == NULL);
}

gfarm_error_t
journal_file_writer_sync(struct journal_file_writer *writer)
{
#ifdef HAVE_FDATASYNC
	if (fdatasync(gfp_xdr_fd(writer->xdr)) == -1)
#else
	if (fsync(gfp_xdr_fd(writer->xdr)) == -1)
#endif
		return (gfarm_errno_to_error(errno));
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
journal_file_writer_flush(struct journal_file_writer *writer)
{
	gfarm_error_t e = gfp_xdr_flush(writer->xdr);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002901,
		    "gfp_xdr_flush : %s", gfarm_error_string(e));
	}
	return (e);
}

const char *
journal_operation_name(enum journal_operation ope)
{
	assert(sizeof(journal_operation_names) / sizeof(char *) ==
		GFM_JOURNAL_OPERATION_MAX);

	if (ope <= 0 || ope >= GFM_JOURNAL_OPERATION_MAX)
		return (NULL);
	return (journal_operation_names[ope]);
}

static gfarm_error_t
journal_write_rec_header(struct gfp_xdr *xdr, gfarm_uint64_t seqnum,
	enum journal_operation ope, size_t data_len, gfarm_uint32_t *crcp)
{
	gfarm_error_t e;
	size_t header_size = journal_rec_header_size();

	if ((e = gfp_xdr_send(xdr, GFARM_JOURNAL_RECORD_HEADER_XDR_FMT,
		GFARM_JOURNAL_RECORD_MAGIC[0],
		GFARM_JOURNAL_RECORD_MAGIC[1],
		GFARM_JOURNAL_RECORD_MAGIC[2],
		GFARM_JOURNAL_RECORD_MAGIC[3],
		seqnum,
		(gfarm_int32_t)ope,
		(gfarm_uint32_t)data_len))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002902,
		    "gfp_xdr_send : %s",
		    gfarm_error_string(e));
		return (e);
	}
	*crcp = gfp_xdr_send_calc_crc32(xdr, 0, -header_size, header_size);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_write_footer(struct gfp_xdr *xdr, size_t data_len,
	gfarm_uint32_t header_crc)
{
	gfarm_error_t e;
	gfarm_uint32_t crc;

	crc = gfp_xdr_send_calc_crc32(xdr, header_crc, -data_len, data_len);
	if ((e = gfp_xdr_send(xdr, "i", crc))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002903,
		    "gfp_xdr_send : %s",
		    gfarm_error_string(e));
	}
	return (e);
}

gfarm_error_t
journal_file_write(struct journal_file *jf, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *arg,
	journal_size_add_op_t size_add_op, journal_send_op_t send_op)
{
	gfarm_error_t e;
	size_t len = 0;
	struct journal_file_writer *writer = &jf->writer;
	struct gfp_xdr *xdr = writer->xdr;
	gfarm_uint32_t crc;
	static const char *diag = "journal_file_write";
#ifdef DEBUG_JOURNAL
	struct journal_file_reader *reader = journal_file_main_reader(jf);
#endif

	assert(jf);
	assert(size_add_op);
	assert(send_op);
	assert(seqnum > 0);
	if ((e = size_add_op(ope, &len, arg)) != GFARM_ERR_NO_ERROR) {
		GFLOG_ERROR_WITH_SN(GFARM_MSG_1002904,
		    "size_add_op", e, seqnum, ope);
		goto end;
	}

	journal_file_mutex_lock(jf, diag);
	if (xdr == NULL) {
		e = GFARM_ERR_INPUT_OUTPUT; /* shutting down */
		goto unlock;
	}
	if ((e = journal_file_check_pos(jf, JOURNAL_RECORD_SIZE(len)))
	    != GFARM_ERR_NO_ERROR)
		goto unlock;
	if ((e = journal_write_rec_header(xdr, seqnum, ope, len, &crc))
	    != GFARM_ERR_NO_ERROR)
		goto unlock;
	if ((e = send_op(ope, arg)) != GFARM_ERR_NO_ERROR) {
		GFLOG_ERROR_WITH_SN(GFARM_MSG_1002905,
		    "send_op", e, seqnum, ope);
		goto unlock;
	}
	if ((e = journal_write_footer(xdr, len, crc))
	    != GFARM_ERR_NO_ERROR)
		goto unlock;
	if ((e = journal_file_writer_flush(writer)) != GFARM_ERR_NO_ERROR)
		goto unlock;
	gfarm_cond_signal(&jf->nonempty_cond, diag, JOURNAL_FILE_STR);
#ifdef DEBUG_JOURNAL
	if (JOURNAL_FILE_READER_IS_INVALID(reader)) {
		gflog_info(GFARM_MSG_1002906,
		    "write seqnum=%llu ope=%s wp=%llu mrp=invalid",
		    (unsigned long long)seqnum, journal_operation_name(ope),
		    (unsigned long long)writer->pos);
	} else {
		gflog_info(GFARM_MSG_1002907,
		    "write seqnum=%llu ope=%s wp=%llu mrp=%llu wr=%u u=%u %%",
		    (unsigned long long)seqnum, journal_operation_name(ope),
		    (unsigned long long)writer->pos,
		    (unsigned long long)reader->pos,
		    JOURNAL_FILE_READER_IS_WRAP(reader),
		    (unsigned int)((JOURNAL_FILE_READER_IS_WRAP(reader) ?
			jf->max_size - JOURNAL_FILE_HEADER_SIZE : 0) +
		    writer->pos - reader->pos) * 100 /
		    (unsigned int)(jf->max_size - JOURNAL_FILE_HEADER_SIZE));
	}
#endif
unlock:
	journal_file_mutex_unlock(jf, diag);
end:
	free(arg);
	return (e);
}

static gfarm_error_t
journal_read_rec_header(struct gfp_xdr *xdr, enum journal_operation *opep,
	gfarm_uint64_t *seqnump, size_t *rlenp)
{
	gfarm_error_t e;
	gfarm_uint64_t seqnum;
	enum journal_operation ope;
	gfarm_uint32_t len, crc1, crc2;
	char magic[GFARM_JOURNAL_MAGIC_SIZE];
	int eof;
	size_t rlen, avail, header_size = journal_rec_header_size();

	memset(magic, 0, sizeof(magic));

	if ((e = gfp_xdr_recv(xdr, 0, &eof,
	    GFARM_JOURNAL_RECORD_HEADER_XDR_FMT,
	    &magic[0], &magic[1], &magic[2], &magic[3],
	    &seqnum, &ope, &len)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002908,
		    "gfp_xdr_recv : %s",
		    gfarm_error_string(e));
		return (e);
	}
	if (ope < 0 || ope >= GFM_JOURNAL_OPERATION_MAX || len < 0 ||
	    len >= JOURNAL_RECORD_SIZE_MAX - header_size) {
		e = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_1002909,
		    "invalid operation id : %s",
		    gfarm_error_string(e));
		return (e);
	}
	crc1 = gfp_xdr_recv_calc_crc32(xdr, 0, -header_size, header_size);
	rlen = len + sizeof(gfarm_uint32_t);
	errno = 0;
	if ((e = gfp_xdr_recv_ahead(xdr, rlen, &avail))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002910,
		    "gfp_xdr_recv_ahead : %s",
		    gfarm_error_string(e));
		return (e);
	}
	if (avail < rlen) {
		e = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_1002911,
		    "record is too short (%lu < %lu): %s",
		    (unsigned long)avail, (unsigned long)rlen,
		    gfarm_error_string(e));
		return (e);
	}
	crc1 = gfp_xdr_recv_calc_crc32(xdr, crc1, 0, len);
	crc2 = gfp_xdr_recv_get_crc32_ahead(xdr, len);
	if (crc1 != crc2) {
		e = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_1002912,
		    "crc check failed %u <-> %u : %s",
		    crc1, crc2, gfarm_error_string(e));
		return (e);
	}
	*opep = ope;
	*seqnump = seqnum;
	*rlenp = rlen + header_size;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
journal_read_purge(struct gfp_xdr *xdr, int len)
{
	gfarm_error_t e;

	if ((e = gfp_xdr_purge(xdr, 1, len))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002913,
		    "gfp_xdr_purge : %s",
		    gfarm_error_string(e));
	}
	return (e);
}

gfarm_error_t
journal_file_read(struct journal_file_reader *reader, void *op_arg,
	journal_read_op_t read_op,
	journal_post_read_op_t post_read_op,
	journal_free_op_t free_op, void *closure, int *eofp)
{
	gfarm_error_t e;
	gfarm_uint64_t seqnum;
	enum journal_operation ope = 0;
	int needs_free = 1, drained = 0;
	size_t len;
	void *obj = NULL;
	struct journal_file *jf = reader->file;
	struct gfp_xdr *xdr = reader->xdr;
	static const char *diag = "journal_file_read";
	size_t avail;
	size_t min_rec_size = journal_rec_header_size()
		+ sizeof(gfarm_uint32_t);

	if (eofp)
		*eofp = 0;
	journal_file_mutex_lock(jf, diag);
	if (journal_file_is_closed(jf)) {
		e = GFARM_ERR_NO_ERROR;
		goto unlock;
	}
	if (xdr == NULL) {
		e = GFARM_ERR_INPUT_OUTPUT; /* shutting down */
		goto unlock;
	}

	if ((e = gfp_xdr_recv_ahead(xdr, JOURNAL_READ_AHEAD_SIZE,
	    &avail)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002914,
		    "gfp_xdr_recv_ahead : %s", gfarm_error_string(e));
		goto unlock;
	}
	if (avail < min_rec_size) { /* no more record */
		if (journal_file_has_writer(jf) == 0) {
			*eofp = 1;
			goto unlock;
		}
		while (avail < min_rec_size) {
			jf->wait_until_nonempty = 1;
			if (JOURNAL_FILE_READER_DRAINED(reader)) {
				journal_file_reader_set_flag(reader,
				    JOURNAL_FILE_READER_F_DRAIN, 0);
				drained = 1;
				e = GFARM_ERR_CANT_OPEN;
				goto unlock;
			}
			gfarm_cond_wait(&jf->nonempty_cond, &jf->mutex,
			    diag, JOURNAL_FILE_STR);
			jf->wait_until_nonempty = 0;
			if ((e = gfp_xdr_recv_ahead(xdr,
			    JOURNAL_READ_AHEAD_SIZE, &avail))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002915,
				    "gfp_xdr_recv_ahead : %s",
				    gfarm_error_string(e));
				goto unlock;
			}
		}
	}
	if ((e = journal_read_rec_header(xdr, &ope, &seqnum, &len))
	    != GFARM_ERR_NO_ERROR)
		goto unlock;
	if (eofp && *eofp)
		goto unlock;
	if ((e = read_op(op_arg, xdr, ope, &obj))
	    != GFARM_ERR_NO_ERROR) {
		GFLOG_ERROR_WITH_SN(GFARM_MSG_1002916,
		    "read_arg_op", e, seqnum, ope);
		goto unlock;
	}
	if ((e = journal_read_purge(xdr, sizeof(gfarm_uint32_t)))
	    != GFARM_ERR_NO_ERROR) /* skip crc */
		goto unlock;
	if ((e = post_read_op(op_arg, seqnum, ope, obj, closure, len,
	    &needs_free)) != GFARM_ERR_NO_ERROR) {
		GFLOG_ERROR_WITH_SN(GFARM_MSG_1002917,
		    "post_read_op", e, seqnum, ope);
		goto unlock;
	}
	reader->la_cache_size -= len;
unlock:
	journal_file_mutex_unlock(jf, diag);
	if (needs_free && obj)
		free_op(op_arg, ope, obj);
	if (drained)
		gfarm_cond_signal(&jf->drain_cond, diag, JOURNAL_FILE_STR);
	return (e);
}

/* recp must be freed by caller */
gfarm_error_t
journal_file_read_serialized(struct journal_file_reader *reader,
	char **recp, gfarm_uint32_t *sizep, gfarm_uint64_t *seqnump, int *eofp)
{
	gfarm_error_t e;
	gfarm_uint64_t seqnum;
	gfarm_uint32_t data_len, data_len2, rec_len;
	/* sizeof header must be larger than header_size */
	unsigned char header[64], *rec = NULL;
	int rlen;
	struct journal_file *jf = reader->file;
	struct gfp_xdr *xdr = reader->xdr;
	size_t avail, header_size = journal_rec_header_size();
	static const char *diag = "journal_file_read_serialized";

	*eofp = 0;
	errno = 0;
	journal_file_mutex_lock(jf, diag);

	if ((e = gfp_xdr_recv_ahead(xdr, header_size, &avail)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002918,
		    "gfp_xdr_recv_ahead : %s", gfarm_error_string(e));
		goto error;
	}
	if ((rlen = gfp_xdr_recv_partial(xdr, 0, header, header_size)) == 0) {
		if (errno == 0) {
			*eofp = 1;
			e = GFARM_ERR_NO_ERROR;
			goto end;
		} else {
			e = GFARM_ERR_INTERNAL_ERROR;
			gflog_error(GFARM_MSG_1002919,
			    "gfp_xdr_recv : %s",
			    gfarm_error_string(gfarm_errno_to_error(errno)));
			goto error;
		}
	}
	seqnum = journal_deserialize_uint64(header + GFARM_JOURNAL_MAGIC_SIZE);
	data_len = journal_deserialize_uint32(header + header_size
		- sizeof(data_len));
	data_len2 = data_len + sizeof(gfarm_uint32_t);
	if ((e = gfp_xdr_recv_ahead(xdr, data_len2, &avail)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002920,
		    "gfp_xdr_recv_ahead : %s", gfarm_error_string(e));
		goto error;
	}
	rec_len = header_size + data_len2;
	GFARM_MALLOC_ARRAY(rec, rec_len);
	if (rec == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_1002921,
		    "%s", gfarm_error_string(e));
		goto error;
	}
	if ((rlen = gfp_xdr_recv_partial(xdr, 0, rec + header_size, data_len2))
	    == 0) {
		gflog_error(GFARM_MSG_1002922,
		    "gfp_xdr_recv : %s", errno != 0 ?
		    gfarm_error_string(gfarm_errno_to_error(errno)) : "eof");
		goto error;
	}
	if (rlen != data_len2) {
		e = GFARM_ERR_INTERNAL_ERROR;
		gflog_error(GFARM_MSG_1002923,
		    "record is too short : %s",
		    gfarm_error_string(e));
		goto error;
	}
	memcpy(rec, header, header_size);
	*recp = (char *)rec;
	*seqnump = seqnum;
	*sizep = rec_len;
	reader->la_cache_size -= rec_len;
	e = GFARM_ERR_NO_ERROR;
	goto end;
error:
	free(rec);
end:
	journal_file_mutex_unlock(jf, diag);
	return (e);
}

void
journal_file_wait_for_read_completion(struct journal_file_reader *reader)
{
	struct journal_file *jf = reader->file;
	static const char *diag = "journal_file_wait_for_read_completion";

	journal_file_mutex_lock(jf, diag);
	journal_file_reader_set_flag(reader, JOURNAL_FILE_READER_F_DRAIN, 1);
	journal_file_mutex_unlock(jf, diag);
	gfarm_cond_signal(&jf->nonempty_cond, diag, JOURNAL_FILE_STR);

	journal_file_mutex_lock(jf, diag);
	while (JOURNAL_FILE_READER_DRAINED(reader))
		gfarm_cond_wait(&jf->drain_cond, &jf->mutex, diag,
		    JOURNAL_FILE_STR);
	journal_file_mutex_unlock(jf, diag);
}

gfarm_error_t
journal_file_write_raw(struct journal_file *jf, int recs_len,
	unsigned char *recs, gfarm_uint64_t *last_seqnump, int *trans_nestp)
{
	gfarm_error_t e;
	unsigned char *rec = recs;
	gfarm_uint32_t data_len, rec_len;
	gfarm_uint64_t seqnum;
	enum journal_operation ope;
	struct journal_file_writer *writer = &jf->writer;
	size_t header_size = journal_rec_header_size();
	static const char *diag = "journal_file_write_raw";

	journal_file_mutex_lock(jf, diag);

	for (;;) {
		seqnum = journal_deserialize_uint64(rec +
		    GFARM_JOURNAL_MAGIC_SIZE);
		ope = journal_deserialize_uint32(rec +
		    GFARM_JOURNAL_MAGIC_SIZE + sizeof(gfarm_uint64_t));
		data_len = journal_deserialize_uint32(rec + header_size -
			sizeof(data_len));
		rec_len = JOURNAL_RECORD_SIZE(data_len);
		if ((e = journal_file_check_pos(jf, rec_len))
		    != GFARM_ERR_NO_ERROR)
			goto unlock;
		if ((e = gfp_xdr_send(writer->xdr, "r", rec_len, rec))
		    != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1002924,
			    "gfp_xdr_send : %s", gfarm_error_string(e));
			goto unlock;
		}
		if ((e = journal_file_writer_flush(writer))
		    != GFARM_ERR_NO_ERROR)
			goto unlock;
		rec += rec_len;
		switch (ope) {
		case GFM_JOURNAL_BEGIN:
			++(*trans_nestp);
			break;
		case GFM_JOURNAL_END:
			--(*trans_nestp);
			break;
		default:
			break;
		}
		if (rec - recs >= recs_len)
			break;
	}
	gfarm_cond_signal(&jf->nonempty_cond, diag, JOURNAL_FILE_STR);
unlock:
	*last_seqnump = seqnum;
	journal_file_mutex_unlock(jf, diag);
	return (e);
}
