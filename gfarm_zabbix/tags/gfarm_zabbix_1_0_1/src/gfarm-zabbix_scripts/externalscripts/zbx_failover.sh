#!/bin/sh
#
# @(#) zbx_failover.sh
#
# Usage:
#   zbx_failover.sh
#
# Description:
#   Failover script for metadata servers on a redundant Gfarm2 filesystem. 
#
###############################################################################

# defines
CONF_FILE=/etc/zabbix/externalscripts/zbx_chk_gfarm.conf

FO_EXEC_USER=zabbix
SYSLOG=/var/log/messages
UPGRADE_CHK_STR="start transforming to the master gfmd"
GFMD_STOP_COMMAND='sudo /etc/init.d/gfmd stop'
GFJOURNAL_CHK_SCRIPT=/etc/zabbix/externalscripts/zbx_chk_gfjournal_gfmd.sh
GFMDLIST_CHK_SCRIPT=/etc/zabbix/externalscripts/zbx_chk_gfmdlist_cli.sh
FO_EXEC_COMMAND='sudo /etc/zabbix/externalscripts/zbx_gfarm2_mds_upgrade.sh'
FO_CHK_COMMAND='gfls -ld /'

# debug
DEBUG=false

log_debug()
{
	[ "X$DEBUG" != Xtrue ] && return	
	logger -t "gfmd_failover" "DEBUG: $@"
}


stop_master_gfmd()
{
	if [ X"$MASTER" != X ]; then
		# check the master was upgraded or not.
		_CHK_UPGRADE_RESULT_=`ssh $MASTER -l $FO_EXEC_USER tail -100 $SYSLOG | grep "$UPGRADE_CHK_STR" | tail -1`
		if [ $? -ne 0 ] || [ X"$_CHK_UPGRADE_RESULT_" != X ]; then
			log_debug "error: master gfmd was already upgraded."
			exit 1
		fi 

		# stop master gfmd
		_GFMD_STOP_RESULT_=`ssh $MASTER -l $FO_EXEC_USER $GFMD_STOP_COMMAND`
		if [ $? -ne 0 ] || [ X"$_GFMD_STOP_RESULT_" = X ]; then
			log_debug "error: can\'t stop master gfmd."
			exit 1
		fi 
	else
		log_debug "error: master metadata server not found."
		exit 1
	fi
}

get_master_candidate_slaves()
{
        _MAX_SEQNUM_FILE_="/var/tmp/seqnum.txt"
        _CANDIDATE_LIST_=`grep "^+ \+slave \+sync \+c \+" $MDS_LIST_PATH | awk '{ print $6 }'`

        echo -n > $_MAX_SEQNUM_FILE_
        if [ ! -f $_MAX_SEQNUM_FILE_ ]; then
		log_debug "error: can\'t create a tmp file to search master candidate."
		exit 1;
	fi

        for _CANDIDATE_ in $_CANDIDATE_LIST_; do
                _MAX_SEQNUM_=`ssh $_CANDIDATE_ sudo $GFJOURNAL_CHK_SCRIPT`
                if [ $? -ne 0 ] || [ X"$_MAX_SEQNUM_" = X ]; then
                        continue
                fi
                echo $_MAX_SEQNUM_ $_CANDIDATE_ >> $_MAX_SEQNUM_FILE_
        done

        sort -nr $_MAX_SEQNUM_FILE_ | awk '{ print $2 };'
        return 0
}


# check config file
if [ -f $CONF_FILE ]; then
	. $CONF_FILE
else
	log_debug "error: $CONF_FILE not found.";
	exit 0;
fi

# create the metadata server list if it doesn't exist.
if [ ! -f $MDS_LIST_PATH ]; then
	/bin/sh $GFMDLIST_CHK_SCRIPT > /dev/null
	if [ $? -ne 0 ]; then
		log_debug "error: metadata server list cache doesn\'t exist."
		exit 1
	fi
fi

# generate the list of candidate slave MDSs for upgrade.
MASTER=`grep "^+ \+master \+- \+m \+" $MDS_LIST_PATH | awk '{ print $6 }'`
if [ X"$MASTER" = X ]; then
	log_debug "error: no master metadata server found."
	exit 1
fi

# generate the list of candidate slave MDSs for upgrade.
SLAVE_LIST=`get_master_candidate_slaves`
if [ X"$SLAVE_LIST" = X ]; then
	log_debug "error: no slave for upgrading."
	exit 1
fi

# stop gfmd on the master MDS host.
stop_master_gfmd

# execute upgrade command on slave servers.
for SLAVE in $SLAVE_LIST
do
	# Upgrade a slave server.
	FO_EXEC_RESULT=`ssh $SLAVE -l $FO_EXEC_USER $FO_EXEC_COMMAND`
	if [ $? -ne 0 ] || [ X"$FO_EXEC_RESULT" = X ]; then
		continue
	fi 

	# Check upgrading is success or not.
	FO_CHK_RESULT=`$FO_CHK_COMMAND`
	if [ $? -ne 0 ] || [ X"$FO_CHK_RESULT" = X ]; then
		continue
	fi 

	# Failover Success and Exit.
	log_debug "success upgrading $SLAVE"
	exit 0
done

log_debug "error: fail to upgrade all slave server."
exit 1
