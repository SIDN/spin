#!/bin/sh
#./tree_test
#./node_cache_test
./node_names_test &&\
#./util_test
#./dns_cache_test
#./arp_test
mv ../*_test-* ./ &&\
#gcov tree_test-tree.c
#gcov node_cache_test-node_cache.c
gcov node_names_test-node_names.c
#gcov arp_test-arp.c
#gcov util_test-util.c
#gcov dns_cache_test-dns_cache.c
