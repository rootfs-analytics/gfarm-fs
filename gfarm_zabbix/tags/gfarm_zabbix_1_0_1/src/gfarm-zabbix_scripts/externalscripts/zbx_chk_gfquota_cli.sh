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
USERS=`gfusage 2>/dev/null | grep -v '^#' | awk '{ print $1 };'`
RESULT=`for u in $USERS; do \
    gfquota -u $u | \
    awk '
        /UserName/           { user=$3 }
        /FileSpace /         { fs=$3 }
        /FileSpaceSoftLimit/ { fslimit=$3 }
        /FileNum /           { fn=$3 }
        /FileNumSoftLimit/   { fnlimit=$3 }
        END{
            if (( fslimit !~ /disable/ && fs >= fslimit ) ||
                ( fnlimit !~ /disable/ && fn >= fnlimit )) 
            {
                print user
            }   
        }'; \
done`

if [ $? != 0 ];
then
    RESULT="gfquota $* error."
fi

if [ X"$RESULT" = X ];
then
    RESULT="---"
fi

echo $RESULT
exit 0

