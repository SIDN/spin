
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g -Wall -Werror

SUBDIRS = @TESTDIR@

noinst_LIBRARIES = libspin.a

libspin_a_SOURCES = pkt_info.c \
					spin_log.h \
					spin_log.c \
					dns_cache.h \
					dns_cache.c \
					tree.h \
					tree.c \
					node_cache.h \
					node_cache.c \
					util.h \
					util.c \
					arp.h \
					arp.c \
					node_names.h \
					node_names.c \
					jsmn.h \
					jsmn.c \
					version.c

#libspin_a_CFLAGS =
#libspin_a_LIBADD =

version.c:
	$(top_srcdir)/../scripts/create_version_c.sh $(top_srcdir)/../VERSION

.PHONY: version.c
