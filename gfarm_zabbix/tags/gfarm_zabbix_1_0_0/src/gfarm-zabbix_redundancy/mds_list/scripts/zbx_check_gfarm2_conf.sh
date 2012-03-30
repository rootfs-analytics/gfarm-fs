#!/bin/sh
#
# @(#) zbx_check_gfarm2_conf.sh
#
# Usage:
#   zbx_check_gfarm2_conf.sh
#
# Description:
#   a script for checking gfarm2.conf on the Gfarm2 file system.
#
###############################################################################

# defines
PATH=$PATH:/usr/local/bin

GFARM2_PREFIX=/usr/local
GFARM2_CONF=$GFARM2_PREFIX/etc/gfarm2.conf
LOCK_FILE=/tmp/zbx_check_gfarm2_conf.lock
RES_FILE=/tmp/gfls_result.txt
ERR_FILE=/tmp/gfls_error.txt
CHK_CMD='sudo -u zabbix gfls -ld /'
TIMEOUT=10

NEW_GFARM2_CONF=$1


# check arguments.
if [ X"$NEW_GFARM2_CONF" = X ]; then
    echo "Specify file name.";
    exit 1
fi

# check gfarm2.conf for the host.
if [ ! -f $GFARM2_CONF ]; then
    echo "$GFARM2_CONF not found.";
    exit 1
fi

# check upload Gfarm2 config file.
if [ ! -f $NEW_GFARM2_CONF ]; then
    echo "$NEW_GFARM2_CONF not found.";
    exit 1
fi


# check the lock file.
if [ -f $LOCK_FILE ]; then
    echo "Another process is using this checker.";
    exit 1
fi

# create the lock file.
touch $LOCK_FILE

# execute to heck Gfarm2 configuration.
if [ -f $GFARM2_CONF.org ]; then
    rm -f $GFARM2_CONF.org
fi

if [ -f $RES_FILE ]; then
    rm -f $RES_FILE
fi

if [ -f $ERR_FILE ]; then
    rm -f $ERR_FILE
fi

# back up gfarm2.conf for the host.
mv -f $GFARM2_CONF $GFARM2_CONF.org
cp -f $NEW_GFARM2_CONF $GFARM2_CONF

$CHK_CMD > $RES_FILE 2> $ERR_FILE &
PID=$!

count=0

while [ $count -lt $TIMEOUT ]; do
    TO_CHK=`ps -ef | awk '{ print $2 }' | grep $PID`
    if [ X$TO_CHK = X ]; then
        break
    fi

    count=`expr $count + 1`
    sleep 1
done

if [ -f $RES_FILE ]; then
    RESULT=`cat $RES_FILE`
fi


# set the return value.
if [ $count -ge $TIMEOUT ]; then
    RESULT="timeout"
elif [ X"$RESULT" = X ]; then
    RESULT="error"
else
    RESULT="success"
fi


# recover gfarm2.conf for the host.
rm -f $GFARM2_CONF
mv -f $GFARM2_CONF.org $GFARM2_CONF

# remove the lock file.
rm -f $LOCK_FILE

echo $RESULT
exit 0
