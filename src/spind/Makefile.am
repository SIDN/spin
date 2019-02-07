
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g -Wall -Werror

SUBDIRS = @TESTDIR@

bin_PROGRAMS = spind

spind_SOURCES = spind.c core2block.c core2kernel.c core2pubsub.c handle_command.h mainloop.h mainloop.c nfqroutines.c
spind_CFLAGS = 
spind_LDADD = $(top_builddir)/lib/libspin.a -lnetfilter_queue

version.c:
	$(top_srcdir)/../scripts/create_version_c.sh $(top_srcdir)/../VERSION

.PHONY: version.c
