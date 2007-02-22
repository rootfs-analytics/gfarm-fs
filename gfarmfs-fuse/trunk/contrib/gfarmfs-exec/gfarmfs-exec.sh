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
#	GFS_USERNAME	global user name in Gfarm   (defaut: $LOGNAME)
#	GFS_MOUNTDIR	mount point		    (defaut: /tmp/$GFS_USERNAME)
#	GFS_WDIR	working directory relative to the home directory
#			in Gfarm file system	    (default: .)
#	GFS_STDOUT	Filename for the standard output (default: STDOUT.$$)
#	GFS_STDERR	Filename for the standard error  (default: STDERR.$$)
#	
# $Id$

ABORT() {
	[ $# -gt 0 ] && echo 1>&2 $*
	exit 1
}

USAGE()
{
	echo "usage: gfarmfs-exec.sh [ -u username ] [ -m mountdir ]"
	echo "           [ -wdir working_directory_from_home ]"
	echo "           [ -stdout file ] [ -stderr file] cmd args ..."
	exit 1
}

PARSE_ARG() {
	while [ $# -gt 0 ]; do
		case $1 in
		-u)    shift; GFS_USERNAME=$1 ;;
		-m)    shift; GFS_MOUNTDIR=$1 ;;
		-wdir) shift; GFS_WDIR=$1 ;;
		-stdout) shift; GFS_STDOUT=$1 ;;
		-stderr) shift; GFS_STDERR=$1 ;;
		-*) USAGE ;;
		*) break ;;
		esac
		shift
	done
	[ $# -lt 1 ] && ABORT "no program specified"
	GFS_PROG=$1
	shift
	GFS_ARGS=$*
}

if [ X"$GFS_PROG" = X ]; then
	PARSE_ARG $*
fi
: ${GFS_USERNAME:=$LOGNAME}
: ${GFS_MOUNTDIR:=/tmp/$GFS_USERNAME}
: ${GFS_WDIR:=.}
: ${GFS_STDOUT:=STDOUT.$$}
: ${GFS_STDERR:=STDERR.$$}

DELETE_MOUNTDIR=0
if [ ! -d $GFS_MOUNTDIR ]; then
	mkdir -p $GFS_MOUNTDIR ||
		ABORT "cannot create a mount point: " $GFS_MOUNTDIR
	DELETE_MOUNTDIR=1
fi
[ -O $GFS_MOUNTDIR ] || ABORT "$GFS_MOUNTDIR: not owned by " $LOGNAME

cd /
# mount Gfarm file system
if ! grep $GFS_MOUNTDIR /etc/mtab > /dev/null; then
	gfarmfs -lsu $GFS_MOUNTDIR || :
fi
# change directory and execute $GFS_PROG with $GFS_ARGS
cd $GFS_MOUNTDIR/$GFS_USERNAME && cd $GFS_WDIR &&
	$GFS_PROG $GFS_ARGS > $GFS_STDOUT 2> $GFS_STDERR
STATUS=$?
cd /
# unmount Gfarm file system
if grep $GFS_MOUNTDIR /etc/mtab > /dev/null; then
	fusermount -u $GFS_MOUNTDIR || :
fi
[ $DELETE_MOUNTDIR = 1 ] && rmdir $GFS_MOUNTDIR

exit $STATUS
