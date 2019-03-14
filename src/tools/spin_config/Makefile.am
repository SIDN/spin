
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g -Wall -Werror

SUBDIRS = @TESTDIR@

bin_PROGRAMS = spin_config
spin_config_SOURCES = spin_config.c
spin_config_CFLAGS =
spin_config_LDADD = $(top_builddir)/lib/libspin.a

version.c:
	$(top_srcdir)/../scripts/create_version_c.sh $(top_srcdir)/../VERSION

.PHONY: version.c
