#!/bin/sh

OUTPUTDIR=./_output
DIFFDIR=./_diffs
TMP_MNTDIR=./_tmp_mnt

EXPECTDIR=./expected

FSYSTEST=./fsystest
GFARMFS=../gfarmfs

##### test mode #####
TESTMODE=$1
DO_TMP=0
DO_GFARMFS=0
DO_FUSEXMP=0

if [ x"${TESTMODE}" = x"all" ]; then
    DO_TMP=1
    DO_GFARMFS=1
    DO_FUSEXMP=1
elif [ x"${TESTMODE}" = x"gfarmfs" ]; then
    DO_TMP=0
    DO_GFARMFS=1
    DO_FUSEXMP=0
elif [ x"${TESTMODE}" = x"fusexmp" ]; then
    DO_TMP=0
    DO_GFARMFS=0
    DO_FUSEXMP=1
else
    echo "usage: $0 <all|gfarmfs|fusexmp>"
    exit 1
fi

##### functions #####
err=0

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
}

check_result()
{
    RESULT=$1
    TESTSTR=$2
    IGNORE=$3

    if [ $RESULT -ne 0 -a $IGNORE -eq 1 ]; then
        echo "IGNORE(${RESULT}): ${TESTSTR}" 1>&2
    elif [ $RESULT -ne 0 ]; then
        err=`expr $err + 1`
        echo "NG(${RESULT}): ${TESTSTR}" 1>&2
    else
        echo "OK(${RESULT}): ${TESTSTR}"
    fi
}

test_common()
{
    TESTDIR=$1
    OUTPUT=${OUTPUTDIR}/${2}
    DIFFOUT=${DIFFDIR}/${2}
    EXPECTED=$3
    TESTSTR=$4

    ${FSYSTEST} ${TESTDIR} > ${OUTPUT} 2>&1
    diff -u ${EXPECTDIR}/${EXPECTED} ${OUTPUT} > ${DIFFOUT} 2>&1
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
    retry=10
    i=0
    while true; do
        fusermount -u $1 > /dev/null 2>&1
        if [ $? -eq 0 ]; then
            return 0;
        fi
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

    EXPECTED=$OUTNAME
    OUTPUT=${OUTPUTDIR}/${OUTNAME}
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

    OUTPUT=${OUTPUTDIR}/${OUTNAME}
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

    EXPECTED=$OUTNAME
    OUTPUT=${OUTPUTDIR}/${OUTNAME}
    TESTNAME="gfarmfs: opt=\"$1\", fuse_opt=\"$2\""
    TESTDIR=${TMP_MNTDIR}/${LOGNAME}  # need gfmkdir gfarm:~

    fuse_common_init "${TESTNAME}" ${OUTNAME}
    $GFARMFS ${OPTIONS} ${TMP_MNTDIR} ${FUSEOPTIONS} > ${OUTPUT} 2>&1
    result=$?
    fuse_common_do_test $result ${TESTDIR} ${OUTNAME}
    fuse_common_final
}

test_fusexmp()
{
    FUSEXMP=$1
    FUSEOPTIONS=$2
    OUTNAME=$3

    EXPECTED=$OUTNAME
    OUTPUT=${OUTPUTDIR}/${OUTNAME}
    TESTNAME="$FUSEXMP: fuse_opt=\"${FUSEOPTIONS}\""
    TESTDIR=${TMP_MNTDIR}/tmp

    which $FUSEXMP 2> $OUTPUT 1> /dev/null
    result=$?
    if [ $result -ne 0 ]; then
        check_result -1 "${TESTNAME}: `cat ${OUTPUT}`" 1  # ignore
        return 0
    fi
    fuse_common_init "${TESTNAME}" ${OUTNAME}
    ${FUSEXMP} ${TMP_MNTDIR} ${FUSEOPTIONS} > ${OUTPUT} 2>&1
    result=$?
    fuse_common_do_test $result ${TESTDIR} ${OUTNAME}
    fuse_common_final
}

##### main #####
init

##### test on /tmp #####
if [ $DO_TMP -eq 1 ]; then
    test_common /tmp slashtmp.out slashtmp.out "/tmp" 
fi


##### test on gfarmfs #####
if [ $DO_GFARMFS -eq 1 ]; then
    test_gfarmfs "" "" gfarmfs_noopt.out
    test_gfarmfs "-nlsu" "" gfarmfs_nlsu.out
    test_gfarmfs "-N2" "" gfarmfs_N2.out
    test_gfarmfs "-b" "" gfarmfs_b.out
    test_gfarmfs "-b" "-o direct_io" gfarmfs_b_direct_io.out
    test_gfarmfs "" "-o direct_io" gfarmfs_direct_io.out
fi

##### fusexmp_fh #####
if [ $DO_FUSEXMP -eq 1 ]; then
    test_fusexmp "fusexmp" "" fusexmp.out
    test_fusexmp "fusexmp_fh" "" fusexmp_fh.out
    test_fusexmp "fusexmp_fh" "-o direct_io" fusexmp_fh_direct_io.out
    test_fusexmp "fusexmp_fh" "-o kernel_cache" fusexmp_fh_kernel_cache.out
    test_fusexmp "fusexmp_fh" "-s" fusexmp_fh_s.out
fi

##### final #####
exit $err
