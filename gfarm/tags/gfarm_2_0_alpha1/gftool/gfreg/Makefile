# $Id$

top_builddir = ../..
top_srcdir = $(top_builddir)
srcdir = .

include $(top_srcdir)/makes/var.mk

SRCS = gfreg_simple.c
OBJS = gfreg_simple.o
PROGRAM = gfreg
CFLAGS = $(COMMON_CFLAGS)
LDLIBS = $(COMMON_LDFLAGS) $(GFARMLIB) $(LIBS)
DEPLIBS = $(DEPGFARMLIB)

all: $(PROGRAM)

include $(top_srcdir)/makes/gfptool.mk

###

$(OBJS): $(DEPGFARMINC) $(GFARMLIB_SRCDIR)/schedule.h
