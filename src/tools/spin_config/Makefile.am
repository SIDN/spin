
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g -Wall -Werror

SUBDIRS = @TESTDIR@

# (the kernelmod references should be removed after userland migration is complete; additional
# shared code from there should also go in lib)
bin_PROGRAMS = spin_config
spin_config_SOURCES = spin_config.c
spin_config_CFLAGS = -I$(top_srcdir)/kernelmod
spin_config_LDADD = -L../kernelmod $(top_builddir)/lib/libspin.a

version.c:
	$(top_srcdir)/../scripts/create_version_c.sh $(top_srcdir)/../VERSION

.PHONY: version.c
