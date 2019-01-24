
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g -Wall -Werror

# TODO: move all $(top_srcdir)/spind references to library
# (the kernelmod references should be removed after userland migration is complete; additional
# shared code from there should also go in lib)
bin_PROGRAMS = spin_print
spin_print_SOURCES = spin_print.c $(top_srcdir)/kernelmod/pkt_info.c $(top_srcdir)/spind/spin_log.c
spin_print_CFLAGS = -I$(top_srcdir)/kernelmod
spin_print_LDADD = -L../kernelmod

version.c:
	$(top_srcdir)/../scripts/create_version_c.sh $(top_srcdir)/../VERSION

.PHONY: version.c
