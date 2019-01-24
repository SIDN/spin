
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g -Wall -Werror

SUBDIRS = @TESTDIR@

bin_PROGRAMS = spin_config spin_print spind
spin_config_SOURCES = spin_config.c util.c tree.c spin_log.c
spin_config_CFLAGS = -I$(top_srcdir)/kernelmod
spin_config_LDADD = -L../kernelmod

spin_print_SOURCES = spin_print.c ../kernelmod/pkt_info.c spin_log.c
spin_print_CFLAGS = -I$(top_srcdir)/kernelmod
spin_print_LDADD = -L../kernelmod

spind_SOURCES = spind.c ../kernelmod/pkt_info.c spin_log.h spin_log.c dns_cache.h dns_cache.c tree.h tree.c node_cache.h node_cache.c util.h util.c arp.h arp.c node_names.h node_names.c jsmn.h jsmn.c netlink_commands.c version.c
spind_CFLAGS = -I$(top_srcdir)/kernelmod
spind_LDADD = -L../kernelmod

version.c:
	$(top_srcdir)/../scripts/create_version_c.sh $(top_srcdir)/../VERSION

.PHONY: version.c
