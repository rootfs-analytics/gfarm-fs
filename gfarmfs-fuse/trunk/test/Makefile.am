noinst_PROGRAMS = fsystest
fsystest_SOURCE = fsystest.c
noinst_SCRIPTS = test.sh
EXTRA_DIST = $(noinst_SCRIPTS) README
SUBDIRS = expected

top-all:
	$(MAKE) -C $(top_builddir) all

check-all: top-all
	sh ./test.sh all

check-gfarmfs: top-all
	sh ./test.sh gfarmfs

check-fusexmp: top-all
	sh ./test.sh fusexmp

check: check-gfarmfs

