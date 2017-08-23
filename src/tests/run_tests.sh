#!/bin/bash
make *_test
TESTS=`find . -name \*_test`
for i in ${TESTS}; do
    $i >& /dev/null
    RESULT=$?
    if [ $RESULT -ne 0 ]; then
        $i
        echo "$i failed"
        exit $RESULT
    else
        echo "$i succeeded"
    fi
done

mv ../*_test-* ./
gcov tree_test-tree.c
gcov node_cache_test-node_cache.c
gcov node_names_test-node_names.c
gcov arp_test-arp.c
gcov util_test-util.c
gcov dns_cache_test-dns_cache.c
gcov netlink_commands_test-netlink_commands.c
rm *.gcda *.gcno
