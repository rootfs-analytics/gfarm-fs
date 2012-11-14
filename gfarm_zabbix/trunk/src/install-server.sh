#! /bin/sh
#
# Install the 'gfarm_zabbix' package for Zabbix server.
#

. ./install.conf

# install(1) command.
INSTALL=install

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
create_zabbix_server_file $SRCFILE
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
create_zabbix_server_file $SRCFILE
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
create_zabbix_server_file $SRCFILE
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
    create_zabbix_server_file $FILE \
	|| { echo "Failed to create the file: $FILE"; exit 1; }
    echo "Create the file: $FILE"
done
