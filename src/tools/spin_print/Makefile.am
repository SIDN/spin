
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g -Wall -Werror

bin_PROGRAMS = spin_print
spin_print_SOURCES = spin_print.c
spin_print_CFLAGS =
spin_print_LDADD = $(top_builddir)/lib/libspin.a

version.c:
	$(top_srcdir)/../scripts/create_version_c.sh $(top_srcdir)/../VERSION

.PHONY: version.c
