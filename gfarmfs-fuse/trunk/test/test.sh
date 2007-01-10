#!/bin/sh

OUTPUTDIR=./output
DIFFDIR=./diffs
TMP_MNTDIR=./_tmp_mnt

EXPECTDIR=./expected

FSYSTEST=./fsystest
GFARMFS=../gfarmfs

export GFARM_PATH_INFO_TIMEOUT=1000

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

##### functions #####
err=0
canceled=0

test_cancel()
{
    echo "canceled (test.sh)"
    canceled=1
}

init()
{
    if [ -e ${OUTPUTDIR} ]; then
        rm -rf ${OUTPUTDIR}
    fi
    mkdir ${OUTPUTDIR}
    if [ $? -ne 0 ]; then
        exit 1
    fi
    if [ -e ${DIFFDIR} ]; then
        rm -rf ${DIFFDIR}
    fi
    mkdir ${DIFFDIR}
    if [ $? -ne 0 ]; then
        exit 1
    fi
    err=0
    trap test_cancel 1
    trap test_cancel 2
}

check_result()
{
    RESULT=$1
    TESTSTR=$2
    IGNORE=$3

    if [ $RESULT -ne 0 -a $IGNORE -eq 1 ]; then
        echo "IGNORE: ${TESTSTR}"
    elif [ $RESULT -ne 0 ]; then
        err=`expr $err + 1`
        echo "NG: ${TESTSTR}"
    else
        echo "OK: ${TESTSTR}"
    fi
}

test_common()
{
    TESTDIR=$1
    OUTPUT=${OUTPUTDIR}/${2}.out
    DIFFOUT=${DIFFDIR}/${2}.out
    EXPECTEDOUT=${EXPECTDIR}/${3}.out
    TESTSTR=$4

    if [ $canceled -eq 1 ];then
        return
    fi
    echo "${TESTSTR}" > ${OUTPUT}
    ${FSYSTEST} ${TESTDIR} >> ${OUTPUT} 2>&1 &
    testpid=$!
    wait $testpid
    if [ $canceled -eq 1 ];then
        wait $testpid
#        cat ${OUTPUT}
    fi
    diff -u ${EXPECTEDOUT} ${OUTPUT} > ${DIFFOUT} 2>&1
    result=$?
    if [ $result -eq 0 ]; then
        rm ${DIFFOUT} > /dev/null 2>&1
        check_result 0 "${TESTSTR}" 0
    else
        check_result -1 "${TESTSTR}: different results" 0
    fi
}

umount_fuse()
{
    MNTDIR=$1

    retry=10
    i=0
    while true; do
        fusermount -u $MNTDIR > /dev/null 2>&1
        if [ $? -eq 0 ]; then
            return 0;
        fi
        # force RELEASE (?)
        ls -l $MNTDIR > /dev/null 2>&1
        if [ $i -ge $retry ]; then
            return 1;
        fi
        i=`expr $i + 1`
    done
}

fuse_common_init()
{
    TESTNAME=$1
    OUTNAME=$2

    OUTPUT=${OUTPUTDIR}/${OUTNAME}.out
    umount_fuse ${TMP_MNTDIR} > /dev/null 2>&1
    rmdir ${TMP_MNTDIR} > /dev/null 2>&1
    if [ -e ${TMP_MNTDIR} ]; then
        echo "TMP_MNTDIR exists" > ${OUTPUT}
        check_result -1 "${TESTNAME}: `cat ${OUTPUT}`" 0
        return 1
    fi
    mkdir ${TMP_MNTDIR} > /dev/null 2>&1
}

fuse_common_do_test()
{
    RESULT=$1
    TESTDIR=$2
    OUTNAME=$3
    TESTNAME=$4

    EXPECTED=${OUTNAME}
    OUTPUT=${OUTPUTDIR}/${OUTNAME}.out
    if [ $RESULT -ne 0 ];then
        check_result -1 "${TESTNAME}: `cat ${OUTPUT}`" 0
        return $RESULT
    fi
    rm ${OUTPUT} > /dev/null 2>&1
    test_common ${TESTDIR} ${OUTNAME} ${EXPECTED} "${TESTNAME}"
}

fuse_common_final()
{
    umount_fuse ${TMP_MNTDIR}
    rmdir ${TMP_MNTDIR} > /dev/null 2>&1
}

mount_gfarmfs()
{
    ${GFARMFS} $1 $2 $3
    return $?
}

test_gfarmfs()
{
    OPTIONS=$1
    FUSEOPTIONS=$2
    OUTNAME=$3

    OUTPUT=${OUTPUTDIR}/${OUTNAME}.out
    ERRLOGOPT="--errlog ${OUTPUTDIR}/errlog-${OUTNAME}"
    TESTNAME="gfarmfs: opt=\"${OPTIONS}\", fuse_opt=\"${FUSEOPTIONS}\""
    TESTDIR=${TMP_MNTDIR}/${LOGNAME}  # need gfmkdir gfarm:~

    if [ $canceled -eq 1 ];then
        return
    fi
    fuse_common_init "${TESTNAME}" ${OUTNAME}
    result=$?
    if [ $result -ne 0 ];then
        return $result
    fi
    $GFARMFS $OPTIONS $ERRLOGOPT $TMP_MNTDIR $FUSEOPTIONS > $OUTPUT 2>&1
    result=$?
    fuse_common_do_test $result ${TESTDIR} ${OUTNAME} "${TESTNAME}"
    fuse_common_final
}

test_fusexmp()
{
    FUSEXMP=$1
    FUSEOPTIONS=$2
    OUTNAME=$3

    OUTPUT=${OUTPUTDIR}/${OUTNAME}.out
    TESTNAME="$FUSEXMP: fuse_opt=\"${FUSEOPTIONS}\""
    TESTDIR=${TMP_MNTDIR}/tmp

    if [ $canceled -eq 1 ];then
        return
    fi
    which $FUSEXMP 2> $OUTPUT 1> /dev/null
    result=$?
    if [ $result -ne 0 ]; then
        check_result -1 "${TESTNAME}: `cat ${OUTPUT}`" 1  # ignore
        return 0
    fi
    fuse_common_init "${TESTNAME}" ${OUTNAME}
    result=$?
    if [ $result -ne 0 ];then
        return $result
    fi
    ${FUSEXMP} ${TMP_MNTDIR} ${FUSEOPTIONS} > ${OUTPUT} 2>&1
    result=$?
    fuse_common_do_test $result ${TESTDIR} ${OUTNAME} "${TESTNAME}"
    fuse_common_final
}

##### main #####
init

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
