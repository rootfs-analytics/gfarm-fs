#! /bin/sh

err=0
canceled=0

test_cancel()
{
    echo "canceled (test.sh)"
    canceled=1
}

test_init()
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
    (${FSYSTEST} ${TESTDIR} 2>&1 | grep -v '^OK' >> ${OUTPUT} 2>&1 ) &
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
        check_result 1 "${TESTSTR}: different results" 0
    fi
}

umount_fuse()
{
    MNTDIR=$1

    retry=10
    i=0
    while true; do
        if [ x"$FUSE_MODE" = x"FUSE25_FreeBSD" ]; then
            umount $MNTDIR > /dev/null 2>&1
        else
            fusermount -u $MNTDIR > /dev/null 2>&1
        fi
        if [ $? -eq 0 ]; then
            return 0
        fi
        # force RELEASE (?)
        ls -l $MNTDIR > /dev/null 2>&1
        if [ $i -ge $retry ]; then
            return 1
        fi
        i=`expr $i + 1`
    done
}

fuse_common_init()
{
    TESTNAME=$1
    OUTNAME=$2
    MNTDIR=$3

    OUTPUT=${OUTPUTDIR}/${OUTNAME}.out
    umount_fuse ${MNTDIR} > /dev/null 2>&1
    rmdir ${MNTDIR} > /dev/null 2>&1
    if [ -e ${MNTDIR} ]; then
        echo "${MNTDIR} exists" > ${OUTPUT}
        check_result 1 "${TESTNAME}: `cat ${OUTPUT}`" 0
        return 1
    fi
    mkdir ${MNTDIR} > /dev/null 2>&1
}

wait_mount()
{
    DIR=$1
    retry=3
    i=0
    while ! ls -ld $DIR > /dev/null 2>&1; do
        if [ $i -ge $retry -o $canceled -eq 1 ]; then
            return 1
        fi
        i=`expr $i + 1`
        sleep 1
    done
    return 0
}

fuse_common_do_test()
{
    RESULT=$1
    TESTDIR=$2
    OUTNAME=$3
    TESTNAME=$4

    EXPECTED=${OUTNAME}
    OUTPUT=${OUTPUTDIR}/${OUTNAME}.out
    wait_mount $TESTDIR
    if [ $? -ne 0 -o $RESULT -ne 0 ]; then
        check_result 1 "${TESTNAME}: `cat ${OUTPUT}`" 0
        return $RESULT
    fi
    rm ${OUTPUT} > /dev/null 2>&1
    test_common ${TESTDIR} ${OUTNAME} ${EXPECTED} "${TESTNAME}"
}

fuse_common_final()
{
    MNTDIR=$1

    umount_fuse ${MNTDIR}
    rmdir ${MNTDIR} > /dev/null 2>&1
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

    if [ $canceled -eq 1 ]; then
        return
    fi
    fuse_common_init "${TESTNAME}" ${OUTNAME} ${TMP_MNTDIR}
    result=$?
    if [ $result -ne 0 ]; then
        return $result
    fi
    $GFARMFS $OPTIONS $ERRLOGOPT $TMP_MNTDIR $FUSEOPTIONS > $OUTPUT 2>&1
    result=$?
    fuse_common_do_test $result ${TESTDIR} ${OUTNAME} "${TESTNAME}"
    fuse_common_final ${TMP_MNTDIR}
}

test_fusexmp()
{
    FUSEXMP=$1
    FUSEOPTIONS=$2
    OUTNAME=$3

    OUTPUT=${OUTPUTDIR}/${OUTNAME}.out
    TESTNAME="$FUSEXMP: fuse_opt=\"${FUSEOPTIONS}\""
    TESTDIR=${TMP_MNTDIR}/tmp

    if [ $canceled -eq 1 ]; then
        return
    fi
    which $FUSEXMP 2> $OUTPUT 1> /dev/null
    result=$?
    if [ $result -ne 0 ]; then
        check_result 1 "${TESTNAME}: `cat ${OUTPUT}`" 1  # ignore
        return 0
    fi
    fuse_common_init "${TESTNAME}" ${OUTNAME} ${TMP_MNTDIR}
    result=$?
    if [ $result -ne 0 ]; then
        return $result
    fi
    if [ x"$FUSE_MODE" = x"FUSE25_FreeBSD" ]; then
        FUSEOPTIONS="-s ${FUSEOPTIONS}"  # instability ?
    fi
    ${FUSEXMP} ${TMP_MNTDIR} ${FUSEOPTIONS} > ${OUTPUT} 2>&1
    result=$?
    fuse_common_do_test $result ${TESTDIR} ${OUTNAME} "${TESTNAME}"
    fuse_common_final ${TMP_MNTDIR}
}
