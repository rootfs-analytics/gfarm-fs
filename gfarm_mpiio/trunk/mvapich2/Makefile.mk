## -*- Mode: Makefile; -*-
## vim: set ft=automake :

if BUILD_AD_GFARM

noinst_HEADERS += adio/ad_gfarm/ad_gfarm.h
AM_CPPFLAGS += -lgfarm 

romio_other_sources +=                   \
		adio/ad_gfarm/ad_gfarm.c\
		adio/ad_gfarm/ad_gfarm_fcntl.c\
		adio/ad_gfarm/ad_gfarm_resize.c\
		adio/ad_gfarm/ad_gfarm_flush.c\
		adio/ad_gfarm/ad_gfarm_open.c\
		adio/ad_gfarm/ad_gfarm_delete.c\
		adio/ad_gfarm/ad_gfarm_hints.c\
		adio/ad_gfarm/ad_gfarm_close.c\
		adio/ad_gfarm/ad_gfarm_read.c\
		adio/ad_gfarm/ad_gfarm_wrcoll.c\
		adio/ad_gfarm/ad_gfarm_rdcoll.c\
		adio/ad_gfarm/ad_gfarm_seek.c\
		adio/ad_gfarm/ad_gfarm_write.c

endif BUILD_AD_GFARM

