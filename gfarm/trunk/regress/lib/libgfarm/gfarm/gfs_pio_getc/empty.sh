#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# arguments are <gfarm_url> <character expected to be returned>
$shell $testbase/getc.sh $gftmp -1
exit_code=$?

gfrm $gftmp
exit $exit_code
