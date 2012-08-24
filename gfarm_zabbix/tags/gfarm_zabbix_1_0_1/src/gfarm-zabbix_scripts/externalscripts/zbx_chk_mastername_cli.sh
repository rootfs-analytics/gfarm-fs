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
if [ -f $MDS_LIST_PATH ]; then
    RESULT=`grep '^+ master' $MDS_LIST_PATH | awk '{ print $6 }'`
else
    RESULT="error: $MDS_LIST not found."
fi

echo $RESULT
exit 0
