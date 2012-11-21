#! /bin/sh
#
# Install the 'gfarm_zabbix' package for Zabbix agent.
#

. ./install.conf

# install(1) command.
INSTALL=install

#
# Create "$1" file from "$1.in".
#
create_file()
{
    [ -f "$1" ] && rm -f "$1"
    sed -e "s|@ZABBIX_CONFDIR@|$ZABBIX_CONFDIR|g" \
        -e "s|@ZABBIX_AGENTD_PIDFILE@|$ZABBIX_AGENTD_PIDFILE|g" \
        -e "s|@ZABBIX_AGENTD_LOGFILE@|$ZABBIX_AGENTD_LOGFILE|g" \
	-e "s|@SYSLOG_FILE@|$SYSLOG_FILE|g" \
	-e "s|@ZABBIX_EXTSCRIPTDIR@|$ZABBIX_EXTSCRIPTDIR|g" \
	-e "s|@ZABBIX_ALERTSCRIPTDIR@|$ZABBIX_ALERTSCRIPTDIR|g" \
	-e "s|@GFARM_BINDIR@|$GFARM_BINDIR|g" \
	-e "s|@ZABBIX_AGENTD_CONFSUBDIR@|$ZABBIX_AGENTD_CONFSUBDIR|g" \
	-e "s|@ZABBIX_SERVER@|$ZABBIX_SERVER|g" \
	-e "s|@ZABBIX_AGENT_HOSTNAME@|$ZABBIX_AGENT_HOSTNAME|g" \
	-e "s|@GFMD_PIDFILE@|$GFMD_PIDFILE|g" \
	-e "s|@GFMD_JOURNALDIR@|$GFMD_JOURNALDIR|g" \
	-e "s|@GFARM_DB_PORT@|$GFARM_DB_PORT|g" \
	-e "s|@GFARM_DB_USER@|$GFARM_DB_USER|g" \
	-e "s|@GFARM_DB_PASSWORD@|$GFARM_DB_PASSWORD|g" \
	-e "s|@IOSTAT_DEVICE@|$IOSTAT_DEVICE|g" \
	"$1.in" > "$1"
}

#
# Make a directory '$ZABBIX_AGENTD_CONFSUBDIR'.
#
DIR=$ZABBIX_AGENTD_CONFSUBDIR
if [ ! -d $DIR ]; then
    $INSTALL -d -m 0755 -o root -g root $DIR \
	|| { echo "Failed to create the directory: $DIR"; exit 1; }
    echo "Create the directory: $DIR"
fi

#
# Install files in 'gfarm-zabbix_scripts/zabbix_agentd.d/' to
# $ZABBIX_AGENTD_CONFSUBDIR.
#
for I in  \
    userparameter_postgresql.conf \
    userparameter_redundant_gfarm.conf; do
    SRCFILE=gfarm-zabbix_scripts/zabbix_agentd.d/$I
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
# Install files in 'gfarm-zabbix_scripts/externalscripts/' to
# $ZABBIX_EXTSCRIPTDIR.
#
for I in \
    zbx_chk_gfarm.conf \
    zbx_chk_gfhost_cli.sh \
    zbx_chk_gfhost_gfsd.sh \
    zbx_chk_gfjournal_gfmd.sh \
    zbx_chk_gfmdhost_cli.sh \
    zbx_chk_gfmdlist_cli.sh \
    zbx_chk_gfmdtype_gfmd.sh \
    zbx_chk_gfquota_cli.sh \
    zbx_chk_gfsched_gfmd.sh \
    zbx_chk_gfsched_gfsd.sh \
    zbx_chk_iostat.sh \
    zbx_chk_mastername_cli.sh \
    zbx_chk_ntp.sh \
    zbx_chk_pgsql.sh \
    zbx_chk_redundancy_cli.sh \
    zbx_failover.sh \
    zbx_gfarm2_mds_upgrade.sh; do
    SRCFILE=gfarm-zabbix_scripts/externalscripts/$I
    DSTFILE=$ZABBIX_EXTSCRIPTDIR/$I
    create_file $SRCFILE
    $INSTALL -c -m 0755 -o root -g root $SRCFILE $DSTFILE \
	|| { echo "Failed to install the file: $DSTFILE"; exit 1; }
    echo "Install the file: $DSTFILE"
done

#
# Install 'zabbix_agentd.conf.sample' file to $ZABBIX_CONFDIR.
#
FILE=zabbix_agentd.conf.sample
SRCFILE=gfarm-zabbix_confs/zabbix/$FILE
DSTFILE=$ZABBIX_CONFDIR/$FILE
create_file $SRCFILE
$INSTALL -c -m 0644 -o root -g root $SRCFILE $DSTFILE \
    || { echo "Failed to install the file: $DSTFILE"; exit 1; }
echo "Install the file: $DSTFILE"

#
# Create the directory of $ZABBIX_AGENTD_PIDFILE.
#
DIR=`dirname $ZABBIX_AGENTD_PIDFILE`
if [ ! -d $DIR ]; then
    $INSTALL -d -m 0755 -o zabbix -g zabbix $DIR \
	|| { echo "Failed to create the directory: $DIR"; exit 1; }
    echo "Create the directory: $DIR"
fi

#
# Create the directory of $ZABBIX_AGENTD_LOGFILE.
#
DIR=`dirname $ZABBIX_AGENTD_LOGFILE`
if [ ! -d $DIR ]; then
    $INSTALL -d -m 0755 -o zabbix -g zabbix $DIR \
	|| { echo "Failed to create the directory: $DIR"; exit 1; }
    echo "Create the directory: $DIR"
fi

#
# Change mode of $SYSLOG_FILE
#
chmod 0644 $SYSLOG_FILE \
    && echo "Set mode (= 0644) of the file: $SYSLOG_FILE"
