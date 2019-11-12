#!/bin/sh

set -e

cd ../..
if [ "$(uname)" = "OpenBSD" ] ; then
	CC=afl-clang make -f Makefile.OpenBSD
else
	CC=afl-clang make -f Makefile.Debian
fi
cd -

export AFL_SKIP_CPUFREQ=1

afl-fuzz -i testcases -o findings -- ../../pcap -r @@

