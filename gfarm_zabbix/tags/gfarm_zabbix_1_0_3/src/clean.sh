#! /bin/sh

find . -name \*.in -type f -print | while read IN_FILE; do
    FILE=`echo "X$IN_FILE" | sed -e 's|^X||' -e 's|\.in$||'`
    [ -f $FILE ] && rm -f $FILE
done
