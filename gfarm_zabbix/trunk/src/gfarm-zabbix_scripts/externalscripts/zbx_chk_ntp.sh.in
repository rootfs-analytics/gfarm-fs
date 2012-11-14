#!/bin/sh

# defines
NTPQ_CMD=/usr/sbin/ntpq

# check config file
if [ ! -f $NTPQ_CMD ]; then
    echo  -1;
    exit 0;
fi

# exec check command
RESULT=`$NTPQ_CMD -p| grep ^+ | awk '{ print $9 };' | sed 's/-//'`

if [ $? != 0 ];
then
    RESULT="ntpq error."
fi

echo $RESULT
exit 0
