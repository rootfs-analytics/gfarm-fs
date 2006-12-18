bin_PROGRAMS = gfarmfs
gfarmfs_SOURCES = gfarmfs.c
gfarmfs_LDADD = $(GFARMFS_LIBS) $(gfarm_libs)
gfarmfs_CFLAGS = $(GFARMFS_CFLAGS)
gfarmfs_CPPFLAGS = -D_REENTRANT -DFUSE_USE_VERSION=$(fuse_interface)
EXTRA_DIST = README.ja ChangeLog.ja gfarmfs.spec
SUBDIRS = contrib test

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
