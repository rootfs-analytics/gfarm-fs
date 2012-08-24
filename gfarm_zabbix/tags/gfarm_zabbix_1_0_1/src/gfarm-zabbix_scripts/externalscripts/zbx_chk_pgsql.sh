#!/bin/sh

# defines
CONF_FILE=/etc/zabbix/externalscripts/zbx_chk_pgsql.conf

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
    version)
	RESULT=`psql --version|head -n1`
	;;
    process)
	RESULT=`psql -U $DB_USER -h $DB_HOST -p $DB_PORT -t -c \
                "select sum(numbackends) from pg_stat_database" \
                $DB_NAME 2> /dev/null`
	;;
    commit)
	RESULT=`psql -U $DB_USER -h $DB_HOST -p $DB_PORT -t -c \
                "select sum(xact_commit) from pg_stat_database" \
                $DB_NAME 2> /dev/null`   
	;;
    rollback)
	RESULT=`psql -U $DB_USER -h $DB_HOST -p $DB_PORT -t -c \
                "select sum(xact_rollback) from pg_stat_database" \
                $DB_NAME 2> /dev/null`
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
