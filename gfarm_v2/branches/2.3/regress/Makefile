top_builddir = ..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

LOG=			log

CHECK_ENV=	LANG=C PATH=$(prefix)/bin:$$PATH

# subdirectories which have to be built
SUBDIRS=	\
	lib/libgfarm/gfarm/gfarm_error_range_alloc \
	lib/libgfarm/gfarm/gfarm_error_to_errno \
	lib/libgfarm/gfarm/gfs_pio_test

check test: all
	@echo ""
	@$(CHECK_ENV) $(srcdir)/regress.sh
	@echo ""

loop: all
	@./loop.sh

all: config

config:
	test -f Makefile || \
	( \
	echo "regress=$(srcdir)"; \
	echo ". $(srcdir)/regress.conf"; \
	) > regress.conf

distclean:
	-$(RM) -f $(LOG)
	-test -f Makefile || $(RM) -f regress.conf

include $(top_srcdir)/makes/subdir.mk
