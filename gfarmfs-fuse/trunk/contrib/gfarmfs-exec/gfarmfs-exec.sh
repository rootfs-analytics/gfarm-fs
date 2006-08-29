#!/bin/sh
#
# wrapper script to execute a program in Gfarm file system using GfarmFS-FUSE
#
#	gfarmfs-exec.sh prog arg ...
#	or
#	GFS_PROG=prog GFS_ARGS="arg ..." gfarmfs-exec.sh
#
# Environment variable:
#
#	GFS_MOUNTDIR	mount point			 (defaut: /tmp/$LOGNAME)
#	GFS_WDIR	working directory relative to the root directory
#			in Gfarm file system		 (default: $LOGNAME)
#	GFS_STDOUT	Filename for the standard output (default: STDOUT.$$)
#	GFS_STDERR	Filename for the standard error  (default: STDERR.$$)
#	
# $Id$

ABORT() {
	[ $# -gt 0 ] && echo 1>&2 $*
	exit 1
}

if [ X"$GFS_PROG" = X ]; then
	[ $# -lt 1 ] && ABORT "no program specified"
	: ${GFS_PROG:=$1}
	shift
	: ${GFS_ARGS:=$*}
fi
: ${GFS_MOUNTDIR:=/tmp/$LOGNAME}
: ${GFS_WDIR:=$LOGNAME}
: ${GFS_STDOUT:=STDOUT.$$}
: ${GFS_STDERR:=STDERR.$$}

if [ ! -d $GFS_MOUNTDIR ]; then
	mkdir -p $GFS_MOUNTDIR ||
		ABORT "cannot create a mount point: " $GFS_MOUNTDIR
fi

gfarmfs -lsfu $GFS_MOUNTDIR || :
cd $GFS_MOUNTDIR && cd $GFS_WDIR &&
	$GFS_PROG $GFS_ARGS > $GFS_STDOUT 2> $GFS_STDERR
STATUS=$?
cd /
fusermount -u $GFS_MOUNTDIR || :

exit $STATUS
