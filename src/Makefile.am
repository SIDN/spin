
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g

bin_PROGRAMS = spin_config spin_print
spin_config_SOURCES = spin_config.c
spin_config_CFLAGS = -I../kernelmod
spin_config_LDADD = -L../kernelmod

spin_print_SOURCES = spin_print.c ../kernelmod/pkt_info.c
spin_print_CFLAGS = -I../kernelmod
spin_print_LDADD = -L../kernelmod
