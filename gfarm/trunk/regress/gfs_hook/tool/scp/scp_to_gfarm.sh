#!/bin/sh

. ./regress.conf

host=`gfhost | sed -n '1p'`
localtmp=$localtop/`basename $hooktmp`

trap 'rm -f $hooktmp; ssh $host rm -f $localtmp; exit $exit_trap' $trap_sigs

if scp $data/1byte $host:$localtmp &&
   scp $host:$localtmp $hooktmp &&
   cmp -s $data/1byte $hooktmp
then
    exit_code=$exit_pass
fi

rm -f $hooktmp
ssh $host rm -f $localtmp
exit $exit_code
