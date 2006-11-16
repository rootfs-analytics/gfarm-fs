bin_PROGRAMS = gfarmfs
gfarmfs_SOURCES = gfarmfs.c
AM_CPPFLAGS = -DFUSE_USE_VERSION=@fuse_interface@ -D_FILE_OFFSET_BITS=64 
EXTRA_DIST = README.ja ChangeLog.ja gfarmfs.spec
SUBDIRS = contrib

testurl = 'http://fuse.cvs.sourceforge.net/*checkout*/fuse/fuse/test/test.c'
testprog = fusetest
testobj = $(testprog).o
testsrc = $(testprog).c

test-get:
	wget $(testurl) -O $(testsrc)

test-build:
	$(CC) -Wall -O2 -c -o $(testobj) $(testsrc)
	$(CC) -o $(testprog) $(testobj)

test-clean:
	-$(RM) $(testprog) $(testobj)
