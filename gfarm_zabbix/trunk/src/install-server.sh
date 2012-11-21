#! /bin/sh
#
# Install the 'gfarm_zabbix' package for Zabbix server.
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
	-e "s|@SYSLOG_FILE@|$SYSLOG_FILE|g" \
	-e "s|@ZABBIX_EXTSCRIPTDIR@|$ZABBIX_EXTSCRIPTDIR|g" \
	-e "s|@ZABBIX_ALERTSCRIPTDIR@|$ZABBIX_ALERTSCRIPTDIR|g" \
	-e "s|@GFARM_BINDIR@|$GFARM_BINDIR|g" \
	-e "s|@ZABBIX_DATADIR@|$ZABBIX_DATADIR|g" \
	-e "s|@APACHE_CONDIR@|$APACHE_CONDIR|g" \
        -e "s|@ZABBIX_SERVER_PIDFILE@|$ZABBIX_SERVER_PIDFILE|g" \
        -e "s|@ZABBIX_SERVER_LOGFILE@|$ZABBIX_SERVER_LOGFILE|g" \
	-e "s|@HTMLDIR@|$HTMLDIR|g" \
	-e "s|@HTMLDIR_USER@|$HTMLDIR_USER|g" \
	-e "s|@HTMLDIR_GROUP@|$HTMLDIR_GROUP|g" \
	-e "s|@PHP_TIMEZONE@|$PHP_TIMEZONE|g" \
	-e "s|@MYSQL_CONFDIR@|$MYSQL_CONFDIR|g" \
	-e "s|@DB_NAME@|$DB_NAME|g" \
	-e "s|@DB_USER@|$DB_USER|g" \
	-e "s|@DB_PASSWORD@|$DB_PASSWORD|g" \
	-e "s|@ZABBIX_NODEID@|$ZABBIX_NODEID|g" \
	"$1.in" > "$1"
}

#
# Make a directory '$HTMLDIR'.
#
DIR=$HTMLDIR
if [ ! -d $DIR ]; then
    $INSTALL -d -m 0755 -o root -g root $DIR \
	|| { echo "Failed to create the directory: $DIR"; exit 1; }
    echo "Create the directory: $DIR"
fi

#
# Install files in 'gfarm-zabbix_redundancy/mds_list/html' to $HTMLDIR.
#
for I in download.php regist.php upload.html; do
    SRCFILE=gfarm-zabbix_redundancy/mds_list/html/$I
    DSTFILE=$HTMLDIR/$I
    $INSTALL -c -m 0644 -o root -g root $SRCFILE $DSTFILE \
	|| { echo "Failed to install the file: $DSTFILE"; exit 1; }
    echo "Install the file: $DSTFILE"
done

#
# Make a directory '$HTMLDIR/files'.
#
DIR=$HTMLDIR/files
if [ ! -d $DIR ]; then
    $INSTALL -d -m 0755 -o $HTMLDIR_USER -g $HTMLDIR_GROUP $DIR \
	|| { echo "Failed to create the directory: $DIR"; exit 1; }
    echo "Create the directory: $DIR"
fi

#
# Make a directory '$ZABBIX_CONFDIR'.
#
DIR=$ZABBIX_CONFDIR
if [ ! -d $DIR ]; then
    $INSTALL -d -m 0755 -o root -g root $DIR \
	|| { echo "Failed to create the directory: $DIR"; exit 1; }
    echo "Create the directory: $DIR"
fi

#
# Install 'zabbix_server.conf.sample' to $ZABBIX_CONFDIR.
#
FILE=zabbix_server.conf.sample
SRCFILE=gfarm-zabbix_confs/zabbix/$FILE
DSTFILE=$ZABBIX_CONFDIR/$FILE
create_file $SRCFILE
$INSTALL -c -m 0600 -o zabbix -g zabbix $SRCFILE $DSTFILE \
    || { echo "Failed to install the file: $DSTFILE"; exit 1; }
echo "Install the file: $DSTFILE"

#
# Make a directory '$MYSQL_CONFDIR'.
#
DIR=$MYSQL_CONFDIR
if [ "X$DIR" != X -a ! -d /.$DIR ]; then
    $INSTALL -d -m 0755 -o root -g root $DIR \
	|| { echo "Failed to create the directory: $DIR"; exit 1; }
    echo "Create the directory: $DIR"
fi

#
# Install 'my.cnf.sample' to $MYSQL_CONFDIR.
#
FILE=my.cnf.sample
SRCFILE=gfarm-zabbix_confs/mysql/$FILE
DSTFILE=$MYSQL_CONFDIR/$FILE
create_file $SRCFILE
if [ "X$MYSQL_CONFDIR" != X ]; then
    $INSTALL -c -m 0644 -o root -g root $SRCFILE $DSTFILE \
	|| { echo "Failed to install the file: $DSTFILE"; exit 1; }
    echo "Install the file: $DSTFILE"
fi

#
# Make a directory '$APACHE_CONFDIR'.
#
DIR=$APACHE_CONFDIR
if [ "X$DIR" != X -a ! -d /.$DIR ]; then
    $INSTALL -d -m 0755 -o root -g root $DIR \
	|| { echo "Failed to create the directory: $DIR"; exit 1; }
    echo "Create the directory: $DIR"
fi

#
# Install 'zabbix.conf.sample' (for Apache HTTPD).
#
FILE=zabbix.conf.sample
SRCFILE=gfarm-zabbix_confs/apache/$FILE
DSTFILE=$APACHE_CONFDIR/$FILE
create_file $SRCFILE
if [ "X$APACHE_CONFDIR" != X ]; then
    $INSTALL -c -m 0644 -o root -g root $SRCFILE $DSTFILE \
	|| { echo "Failed to install the file: $DSTFILE"; exit 1; }
    echo "Install the file: $DSTFILE"
fi

#
# Create template files in 'gfarm-zabbix_templates' sub-directory.
#
for I in \
    Template_Gfarm_cli.xml \
    Template_Gfarm_common.xml \
    Template_Gfarm_gfmd.xml \
    Template_Gfarm_gfsd.xml \
    Template_Gfarm_redundant_cli.xml \
    Template_Gfarm_redundant_gfmd.xml \
    Template_Gfarm_redundant_gfsd.xml \
    Template_Gfarm_zabbix.xml; do
    FILE=gfarm-zabbix_templates/$I
    create_file $FILE \
	|| { echo "Failed to create the file: $FILE"; exit 1; }
    echo "Create the file: $FILE"
done

#
# Create the directory of $ZABBIX_SERVER_PIDFILE.
#
DIR=`dirname $ZABBIX_SERVER_PIDFILE`
if [ ! -d $DIR ]; then
    $INSTALL -d -m 0755 -o zabbix -g zabbix $DIR \
	|| { echo "Failed to create the directory: $DIR"; exit 1; }
    echo "Create the directory: $DIR"
fi

#
# Create the directory of $ZABBIX_SERVER_LOGFILE.
#
DIR=`dirname $ZABBIX_SERVER_LOGFILE`
if [ ! -d $DIR ]; then
    $INSTALL -d -m 0755 -o zabbix -g zabbix $DIR \
	|| { echo "Failed to create the directory: $DIR"; exit 1; }
    echo "Create the directory: $DIR"
fi
