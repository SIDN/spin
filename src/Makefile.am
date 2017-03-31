
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS}

sbin_PROGRAMS = nflog_print nflog2mqtt

nflog_print_SOURCES = nflog_print.c nflog.c
nflog_print_LDFLAGS = -lnetfilter_log -lnfnetlink

nflog2mqtt_SOURCES = nflog2mqtt.c nflog.c
nflog2mqtt_LDFLAGS = -lnetfilter_log -lnfnetlink

