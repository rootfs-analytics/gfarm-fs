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
RESULT=`gfjournal -m /var/gfarm-metadata/journal/0000000000.gmj`

if [ $? != 0 ];
then
    echo  -1;
    exit 0;
fi

echo $RESULT

exit 0
