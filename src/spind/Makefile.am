
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g -Wall -Werror

bin_PROGRAMS = spind

spind_SOURCES = spind.c core2block.c core2kernel.c core2pubsub.c handle_command.h mainloop.h mainloop.c nfqroutines.c core2nflog_dns.c core2conntrack.c config_common.c config_uci.c statistics.c nflogroutines.c rpc_json.c rpc_ubus.c spindata.c cJSON.c
#spind_CFLAGS =
spind_LDADD = -lubox -lubus -lblobmsg_json $(top_builddir)/lib/libspin.a

version.c:
	$(top_srcdir)/../scripts/create_version_c.sh $(top_srcdir)/../VERSION

.PHONY: version.c
