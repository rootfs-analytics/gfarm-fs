#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# arguments are <gfarm_url> <size of bytes>
$shell $testbase/gets.sh $gftmp
exit_code=$?

gfrm $gftmp
exit $exit_code
