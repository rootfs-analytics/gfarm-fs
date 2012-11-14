#! /bin/sh
#
# Setup MySQL database for 'gfarm_zabbix'.
# Please note that this script assumes MySQL server (mysqld) is running.
#

. ./install.conf

(
    echo "create database $DB_NAME;"
) | mysql --user=root \
    || { echo "Failed to create the database: $DB_NAME"; exit 1; }
echo "Create the database: $DB_NAME"

(
    echo "grant all on $DB_NAME.* to $DB_USER@localhost "
    echo "    identified by '$DB_PASSWORD';" \
) | mysql --user=root \
    || { echo "Failed to create the user: $DB_USER"; exit 1; }
echo "Create the user: $DB_USER"

for I in \
    $ZABBIX_DOCDIR/schema/mysql.sql \
    $ZABBIX_DOCDIR/data/data.sql \
    $ZABBIX_DOCDIR/data/images_mysql.sql; do
    cat $I | mysql --user=$DB_USER --password="$DB_PASSWORD" $DB_NAME \
	|| { echo "Failed to execute the SQL file: $I"; exit 1; }
    echo "Execute the SQL file: $I"
done
