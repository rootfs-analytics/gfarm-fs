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
RESULT=`gfmdhost $*`

if [ $? != 0 ];
then
    RESULT="gfmdhost $* error."
fi

echo $RESULT
exit 0
