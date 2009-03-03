#!/bin/sh

if [ ! -f globus_gridftp_server_gfarm.c ]; then
    echo "Here is not gfarm2-dsi working directory."
    exit 1
fi

CONF=./setup.conf
if [ -f $CONF ]; then
    . $CONF
fi
if [ x${GFARM} = x -o ! -f ${GFARM}/include/gfarm/gfarm.h ]; then
    gfhost=`which gfhost`
    if [ x${gfhost} != x ]; then
	gfarm_bin=`dirname $gfhost`
	if [ x${gfarm_bin} != x ]; then
	    GFARM=`dirname $gfarm_bin`
	fi
    fi
fi
if [ x${GFARM} = x -o ! -f ${GFARM}/include/gfarm/gfarm.h ]; then
    echo "Please set GFARM in ${CONF}."
    exit 1
fi
if [ x${GLOBUS_LOCATION} = x ]; then
    echo "Please set GLOBUS_LOCATION environment variable."
    exit 1
fi
FTPSERVER=${GLOBUS_LOCATION}/sbin/globus-gridftp-server
if [ ! -f $FTPSERVER ]; then
    echo "$FTPSERVER is not found."
    exit 1
fi
if [ x${FLAVOR} = x ]; then
    FLAVOR=`ldd ${FTPSERVER} | awk '($1 ~ /^libglobus_gridftp_server_control_/){name=gensub(/^libglobus_gridftp_server_control_(.+)\.so.*/,"\\\\1","g",$1);print name}'`
fi
if [ x${FLAVOR} = x ]; then
    echo "Please set FLAVOR in ${CONF}."
    exit 1
fi

echo "GLOBUS_LOCATION = $GLOBUS_LOCATION"
echo "FLAVOR = $FLAVOR"
echo "GFARM = $GFARM"

f_prefix=`expr "${FLAVOR}" : '\(.*\)pthr'`
f_pthr=`expr "${FLAVOR}" : '.*\(pthr\)'`
if [ x${f_pthr} = xpthr ]; then
    echo "Please copy ${GLOBUS_LOCATION}/sbin/${f_prefix}/shared/globus-gridftp-server into ${GLOBUS_LOCATION}/sbin/globus-gridftp-server."
    exit 1
fi

sed -e "s/@FLAVOR@/$FLAVOR/g" \
    -e "s|@GFARM@|$GFARM|g" \
    -e "s|@GLOBUS_LOCATION@|$GLOBUS_LOCATION|g" \
    Makefile.in > Makefile

HEADER=globus_makefile_header
if [ ! -s $HEADER ]; then
    ${GLOBUS_LOCATION}/bin/globus-makefile-header \
	-flavor=${FLAVOR} globus_gridftp_server > $HEADER
fi
if [ ! -s $HEADER ]; then
    echo "cannot create ${HEADER}."
    exit 1
fi

make
rv=$?

if [ $rv = 0 ]; then
    echo 'Please run `make install'"' to install."
fi

exit $rv
