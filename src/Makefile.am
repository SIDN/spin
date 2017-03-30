
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS}

sbin_PROGRAMS = spin_nflog

spin_nflog_SOURCES = nflog_printd.c
spin_nflog_LDFLAGS = -lnetfilter_log -lnfnetlink
