# $Id$

top_objdir = ../..
top_srcdir = $(top_objdir)
srcdir = .

include $(top_srcdir)/makes/var.mk
include $(srcdir)/../Makefile.inc

CFLAGS = $(COMMON_CFLAGS) -I$(GFUTIL_SRCDIR) -I$(GFARMLIB_SRCDIR) \
	$(openssl_includes) \
	-DGFARM_DEFAULT_BINDIR=\"$(default_bindir)\"
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

PROGRAM = gfsd
SRCS =	gfsd.c loadavg.c
OBJS =	gfsd.o loadavg.o

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC) \
	$(GFUTIL_SRCDIR)/gfutil.h \
	$(GFARMLIB_SRCDIR)/iobuffer.h \
	$(GFARMLIB_SRCDIR)/xxx_proto.h \
	$(GFARMLIB_SRCDIR)/io_fd.h \
	$(GFARMLIB_SRCDIR)/param.h \
	$(GFARMLIB_SRCDIR)/sockopt.h \
	$(GFARMLIB_SRCDIR)/host.h \
	$(GFARMLIB_SRCDIR)/auth.h \
	$(GFARMLIB_SRCDIR)/gfs_proto.h \
	$(GFARMLIB_SRCDIR)/gfs_client.h
