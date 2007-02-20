#! /bin/sh

OUTPUTDIR=./output
DIFFDIR=./diffs
TMP_MNTDIR=./_tmp_mnt

FSYSTEST=./fsystest
GFARMFS=../gfarmfs
TESTSH=./test.sh
FUNCSH=./func.sh
CONFSH=./conf.sh

export GFARM_PATH_INFO_TIMEOUT=1000

##### check current directory #####
if [ ! -f $TESTSH -o ! -f $FUNCSH -o ! -f $CONFSH ]; then
    exit 1
fi

##### initialize #####
. $CONFSH
. $FUNCSH
test_init

##### expected directory #####
if [ x"$FUSE_MODE" = x"FUSE25_Linux" ]; then
    EXPECTDIR=./expected/default
elif [ x"$FUSE_MODE" = x"FUSE25_Linux26_old" ]; then
    EXPECTDIR=./expected/fuse25_linux26_old
elif [ x"$FUSE_MODE" = x"FUSE22_Linux" ]; then
    EXPECTDIR=./expected/default  ### TODO fuse22_linux
elif [ x"$FUSE_MODE" = x"FUSE25_FreeBSD" ]; then
    EXPECTDIR=./expected/freebsd
else ### unknown
    EXPECTDIR=./expected/default
fi
echo "expected directory: ${EXPECTDIR} (${FUSE_MODE})"

##### test mode #####
TESTMODE=$1
DO_TMP=0
DO_GFARMFS=0
DO_GFARMFS_OLD=0
DO_FUSEXMP=0

if [ x"${TESTMODE}" = x"all" ]; then
    DO_TMP=1
    DO_GFARMFS=1
    DO_GFARMFS_OLD=1
    DO_FUSEXMP=1
elif [ x"${TESTMODE}" = x"tmp" ]; then
    DO_TMP=1
elif [ x"${TESTMODE}" = x"gfarmfs" ]; then
    DO_GFARMFS=1
elif [ x"${TESTMODE}" = x"oldgfarmfs" ]; then
    DO_GFARMFS_OLD=1
elif [ x"${TESTMODE}" = x"fusexmp" ]; then
    DO_FUSEXMP=1
else
    echo "usage: $0 <all|gfarmfs|oldgfarmfs|fusexmp|tmp>"
    exit 1
fi

##### test on /tmp #####
if [ $DO_TMP -eq 1 ]; then
    test_common /tmp slashtmp slashtmp "/tmp" 
fi

##### test on gfarmfs #####
if [ $DO_GFARMFS -eq 1 ]; then
    test_gfarmfs "" "" gfarmfs_noopt
    test_gfarmfs "-nlsu" "" gfarmfs_nlsu
    test_gfarmfs "-nlsu" "-o default_permissions" gfarmfs_defperm
    test_gfarmfs "-nlsu" "-o attr_timeout=0" gfarmfs_attr0
    test_gfarmfs "-nlsu -N2" "" gfarmfs_N2
    test_gfarmfs "-nlsu -b" "" gfarmfs_b
    test_gfarmfs "-nlsu -b" "-o direct_io" gfarmfs_b_direct_io
    test_gfarmfs "-nlsu" "-o direct_io" gfarmfs_direct_io
fi
if [ $DO_GFARMFS_OLD -eq 1 ]; then
    test_gfarmfs "--oldio -nlsu" "-o attr_timeout=0" gfarmfs_oldio
    test_gfarmfs "--oldio -nlsub" "-o attr_timeout=0" gfarmfs_oldio_b
    test_gfarmfs "--oldio -nlsuF" "-o attr_timeout=0" gfarmfs_oldio_F
fi

##### fusexmp_fh #####
if [ $DO_FUSEXMP -eq 1 ]; then
    test_fusexmp "fusexmp" "" fusexmp
    test_fusexmp "fusexmp_fh" "" fusexmp_fh
    test_fusexmp "fusexmp_fh" "-o direct_io" fusexmp_fh_direct_io
    test_fusexmp "fusexmp_fh" "-o kernel_cache" fusexmp_fh_kernel_cache
    test_fusexmp "fusexmp_fh" "-s" fusexmp_fh_s
    test_fusexmp "fusexmp_fh" "-o default_permissions" fusexmp_fh_defperm
fi

##### final #####
exit $err
