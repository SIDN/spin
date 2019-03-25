AM_CPPFLAGS = -I$(top_srcdir)/include -Wall -Werror -DTESTDATA_PATH='"$(abs_srcdir)/testdata"'
AM_CFLAGS = ${regular_CFLAGS} -g 

CLEANFILES = *.gcda *.gcno *.gcov

bin_PROGRAMS = tree_test node_cache_test arp_test node_names_test util_test dns_cache_test

tree_test_SOURCES = tree_test.c ../tree.c ../util.c ../spin_log.c
tree_test_CFLAGS = -I../ -fprofile-arcs -ftest-coverage
tree_test_LDFLAGS = -L../

node_cache_test_SOURCES = ../node_names.c ../util.c ../tree.c ../pkt_info.c ../arp.c ../spin_log.c  node_cache_test.c
node_cache_test_CFLAGS = -I../ -fprofile-arcs -ftest-coverage
node_cache_test_LDFLAGS = -L../

dns_cache_test_SOURCES = dns_cache_test.c ../dns_cache.c ../util.c ../tree.c ../pkt_info.c ../spin_log.c
dns_cache_test_CFLAGS = -I../ -fprofile-arcs -ftest-coverage
dns_cache_test_LDFLAGS = -L../


arp_test_SOURCES = ../util.c ../tree.c ../spin_log.c arp_test.c
arp_test_CFLAGS = -I../ -fprofile-arcs -ftest-coverage
arp_test_LDFLAGS = -L../

node_names_test_SOURCES = node_names_test.c ../node_names.c ../util.c ../tree.c ../spin_log.c
node_names_test_CFLAGS = -I../ -fprofile-arcs -ftest-coverage
node_names_test_LDFLAGS = -L../

util_test_SOURCES = util_test.c ../util.c ../tree.c ../pkt_info.c ../spin_log.c
util_test_CFLAGS = -I../ -fprofile-arcs -ftest-coverage
util_test_LDFLAGS = -L../

all-local:
	$(srcdir)/run_tests.sh
