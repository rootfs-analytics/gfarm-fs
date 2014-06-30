#! /bin/sh
#
# Install the 'gfarm_zabbix' package for Zabbix agent.
#

. ./install.conf

# install(1) command.
INSTALL=install

# Directory where external scripts for Zabbix reside.
ZABBIX_EXTSCRIPTDIR=$ZABBIX_CONFDIR/externalscripts

# Directory where configuration files for Zabbix agentd reside.
ZABBIX_AGENTD_CONFSUBDIR=$ZABBIX_CONFDIR/zabbix_agentd.d

#
# Create "$1" file from "$1.in".
#
create_file()
{
    [ -f "$1".in ] || return 0
    [ -f "$1" ] && rm -f "$1"
    sed \
        -e "s|@GFARM_BINDIR@|$GFARM_BINDIR|g" \
        -e "s|@GFMD_JOURNALDIR@|$GFMD_JOURNALDIR|g" \
        -e "s|@GFMD_PIDFILE@|$GFMD_PIDFILE|g" \
        -e "s|@SYSLOG_FILE@|$SYSLOG_FILE|g" \
        -e "s|@ZABBIX_CONFDIR@|$ZABBIX_CONFDIR|g" \
        -e "s|@ZABBIX_EXTSCRIPTDIR@|$ZABBIX_EXTSCRIPTDIR|g" \
        "$1.in" > "$1"
}

#
# Install files in 'scripts/zabbix_agentd.d/' to
# $ZABBIX_AGENTD_CONFSUBDIR.
#
for I in \
    userparameter_gfarm.conf; do
    SRCFILE=scripts/zabbix_agentd.d/$I
    DSTFILE=$ZABBIX_AGENTD_CONFSUBDIR/$I
    create_file $SRCFILE
    $INSTALL -c -m 0755 -o root -g root $SRCFILE $DSTFILE \
        || { echo "Failed to install the file: $DSTFILE"; exit 1; }
    echo "Install the file: $DSTFILE"
done

#
# Make a directory '$ZABBIX_EXTSCRIPTDIR'.
#
DIR=$ZABBIX_EXTSCRIPTDIR
if [ ! -d $DIR ]; then
    $INSTALL -d -m 0755 -o root -g root $DIR \
        || { echo "Failed to create the directory: $DIR"; exit 1; }
    echo "Create the directory: $DIR"
fi

#
# Install files in 'scripts/externalscripts/' to
# $ZABBIX_EXTSCRIPTDIR.
#
for I in \
    gfarm_generic_client_gfhost.sh \
    gfarm_generic_client_gfmdhost.sh \
    gfarm_conf.inc \
    gfarm_gfmd_failover.pl \
    gfarm_gfmd_postgresql.sh \
    gfarm_gfsd_gfhost.sh \
    gfarm_represent_client_gfhost.sh \
    gfarm_represent_client_gfmdhost.sh \
    gfarm_utils.inc; do
    SRCFILE=scripts/externalscripts/$I
    DSTFILE=$ZABBIX_EXTSCRIPTDIR/$I
    create_file $SRCFILE
    $INSTALL -c -m 0755 -o root -g root $SRCFILE $DSTFILE \
        || { echo "Failed to install the file: $DSTFILE"; exit 1; }
    echo "Install the file: $DSTFILE"
done

#
# Change mode of $SYSLOG_FILE
#
chmod 0644 $SYSLOG_FILE \
    && echo "Set mode (= 0644) of the file: $SYSLOG_FILE"
