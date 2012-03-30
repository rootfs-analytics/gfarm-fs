#!/bin/sh
#
# @(#) zbx_gfarm2_mds_upgrade.sh
#
# Usage:
#   zbx_gfarm2_mds_upgrade.sh
#
# Description:
#   Upgrade script for a slave metadata server on a redundant Gfarm2 file
#   system.
#
###############################################################################

# defines
PID_FILE=/var/run/gfmd.pid

# execute upgrade command
PID=`cat $PID_FILE`

kill -USR1 $PID

if [ $? -eq 0 ]; then
    echo "success"
else
    echo "error"
fi 

exit 0
