#!/bin/sh

. ./regress.conf

trap 'rm -rf $hooktmp; exit $exit_trap' $trap_sigs

if mkdir $hooktmp &&
   touch $hooktmp/AXAXAX &&
   sleep 1 &&
   touch $hooktmp/BZBZBZ &&
   sleep 1 &&
   touch $hooktmp/AXAXAX &&
   ls -t $hooktmp/AXAXAX $hooktmp/BZBZBZ | head -1 | grep AXAXAX >/dev/null
then
	exit_code=$exit_pass
fi

rm -rf $hooktmp

case `gfarm.arch.guess` in
i386-fedora5-linux)
	# documented in README.hook.*, its cause haven't been investigated yet.
	case $exit_code in
	$exit_pass)	exit_code=$exit_xpass;;
	$exit_fail)	exit_code=$exit_xfail;;
	esac;;
esac
exit $exit_code
