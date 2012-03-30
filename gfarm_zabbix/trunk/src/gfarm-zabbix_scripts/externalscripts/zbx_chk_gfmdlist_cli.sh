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
MDS_LIST=`gfmdhost -l`

if [ $? = 0 ];
then
    if [ -f $MDS_LIST_PATH ];
    then
        OLD_MDS_LIST_PATH=$MDS_LIST_PATH.old

        mv -f  $MDS_LIST_PATH $OLD_MDS_LIST_PATH
    fi
    cat << EOF > $MDS_LIST_PATH
$MDS_LIST
EOF

    RESULT="gfmdhost -l same."

    if [ -f $OLD_MDS_LIST_PATH ];
    then
        DIFF=`diff $OLD_MDS_LIST_PATH $MDS_LIST_PATH | head -1`
        if [ X"$DIFF" != X ];
        then
            RESULT="gfmdhost -l changed."
        fi
    fi
else
    RESULT="gfmdhost -l error."
fi

echo $RESULT
exit 0
