#! /bin/sh
#
# Install 'gfarm2.conf' editor.
#

. ./install.conf

# install(1) command.
INSTALL=install

# Directory where edited 'gfarm2.conf' will be stored.
EDITOR_SKELETON_DIR=$EDITOR_HTMLDIR/skeleton

# Directory where 'metadataserver_list.log' will be placed by an external
# script of Zabbix.
EDITOR_GFMDLIST_DIR=$EDITOR_HTMLDIR/gfmdlist

#
# Create "$1" file from "$1.in".
#
create_file()
{
    [ -f "$1".in ] || return 0
    [ -f "$1" ] && rm -f "$1"
    sed \
        -e "s|@GFARM_BINDIR@|$GFARM_BINDIR|g" \
        -e "s|@ZABBIX_CONFDIR@|$ZABBIX_CONFDIR|g" \
        -e "s|@EDITOR_GFMDLIST_DIR@|$EDITOR_GFMDLIST_DIR|g" \
        "$1.in" > "$1"
}

#
# Make a directory '$EDITOR_HTMLDIR'.
#
DIR=$EDITOR_HTMLDIR
if [ ! -d $DIR ]; then
    $INSTALL -d -m 0755 -o root -g root $DIR \
        || { echo "Failed to create the directory: $DIR"; exit 1; }
    echo "Create the directory: $DIR"
fi

#
# Install files 'gfarm-zabbix_editor/*.php' to $EDITOR_HTMLDIR.
#
for I in \
    common.php \
    download.php \
    edit.php \
    index.php \
    save.php; do
    SRCFILE=gfarm-zabbix_editor/$I
    DSTFILE=$EDITOR_HTMLDIR/$I
    $INSTALL -c -m 0755 -o root -g root $SRCFILE $DSTFILE \
        || { echo "Failed to install the file: $DSTFILE"; exit 1; }
    echo "Install the file: $DSTFILE"
done

#
# Make a directory '$EDITOR_SKELETON_DIR'.
#
DIR=$EDITOR_SKELETON_DIR
if [ ! -d $DIR ]; then
    $INSTALL -d -m 0755 -o $EDITOR_HTMLDIR_USER -g $EDITOR_HTMLDIR_GROUP $DIR \
        || { echo "Failed to create the directory: $DIR"; exit 1; }
    echo "Create the directory: $DIR"
fi

#
# Create initial 'gfarm2.conf' file.
#
SRCFILE=gfarm-zabbix_editor/gfarm2.conf
DSTFILE=$EDITOR_SKELETON_DIR/gfarm2.conf
if [ ! -f $DSTFILE ]; then
    $INSTALL -c -m 0644 -o $EDITOR_HTMLDIR_USER -g $EDITOR_HTMLDIR_GROUP \
        $SRCFILE $DSTFILE \
        || { echo "Failed to install the file: $DSTFILE"; exit 1; }
    echo "Install the file: $DSTFILE"
fi

#
# Make a directory '$EDITOR_GFMDLIST_DIR'.
#
DIR=$EDITOR_GFMDLIST_DIR
if [ ! -d $DIR ]; then
    $INSTALL -d -m 0755 -o zabbix -g zabbix $DIR \
        || { echo "Failed to create the directory: $DIR"; exit 1; }
    echo "Create the directory: $DIR"
fi

#
# Install 'gfarm-zabbix_editor/gfmdlist.sh' to '$ZABBIX_CONFDIR'.
#
SRCFILE=gfarm-zabbix_editor/gfmdlist.sh
DSTFILE=$ZABBIX_CONFDIR/gfmdlist.sh
create_file $SRCFILE
$INSTALL -c -m 0755 -o root -g root $SRCFILE $DSTFILE \
    || { echo "Failed to install the file: $DSTFILE"; exit 1; }

echo "Install the file: $DSTFILE"
echo ""
echo "Please add the following lines to a crontab file of user 'zabbix':"
echo ""
echo "    # Run 'gfmdhost -l' every five minutes."
echo "    */5 * * * * $DSTFILE"
echo ""
echo "or add the following lines to a file under /etc/cron.d/:"
echo ""
echo "    # Run 'gfmdhost -l' every five minutes."
echo "    */5 * * * * zabbix $DSTFILE"
echo ""
