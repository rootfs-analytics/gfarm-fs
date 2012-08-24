#!/bin/sh
#
# @(#) zbx_generate_mdslist.sh
#
# Usage:
#   zbx_generate_mdslist.sh
#
# Description:
#   genarate a gfarm2.conf line for "metadb_server_list" on the Gfarm2 file
#   system.
#
###############################################################################

# defines
GFARM2_PREFIX=/usr/local
MDS_LIST=/var/tmp/metadataserver_list.log

# generate metadata serve list.
if [ ! -f $MDS_LIST ];
then
    echo "$MDS_LIST not found."
    exit 1;
else
    echo -n 'metadb_server_list '
    cat $MDS_LIST | awk '{ print $6":"$7 }' | tr '\n' ' '
    echo
fi

exit 0;
