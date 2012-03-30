#!/bin/sh

# defines
CONF_FILE=/etc/zabbix/externalscripts/zbx_chk_gfarm.conf

# check config file
if [ -f $CONF_FILE ];
    then
    . $CONF_FILE
else
    echo  -1;
    exit 0;
fi

# exec check command
RESULT=-1
case "$1" in
    read)
        RESULT=`iostat -m | grep "$DEVICE" | awk '{ print $3 };'`
        ;;

    write)
        RESULT=`iostat -m | grep "$DEVICE" | awk '{ print $4 };'`
        ;;

    total_read)
        RESULT=`iostat -m | grep "$DEVICE" | awk '{ print $5 };'`
        ;;

    total_write)
        RESULT=`iostat -m | grep "$DEVICE" | awk '{ print $6 };'`
        ;;

    *)
        RESULT=-1
esac

if [ $? != 0 ];
    then
    RESULT=-1
fi

echo $RESULT
exit 0
