# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SRCS = gfps.c
OBJS = gfps.o
PROGRAM = gfps
CFLAGS = $(COMMON_CFLAGS) -I$(GFARMLIB_SRCDIR) $(openssl_includes)
LDLIBS = $(COMMON_LDLIBS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/prog.mk

###

$(OBJS): $(DEPGFARMINC)
