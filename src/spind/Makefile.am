
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g -Wall -Werror

bin_PROGRAMS = spind

spind_SOURCES = spind.c core2block.c core2pubsub.c handle_command.h mainloop.h mainloop.c nfqroutines.c core2nflog_dns.c core2conntrack.c config_common.c config_uci.c statistics.c nflogroutines.c rpc_json.c spindata.c spinhook.c cJSON.c

#spind_CFLAGS =
spind_LDADD = $(top_builddir)/lib/libspin.a

if USE_UBUS
    spind_SOURCES += rpc_ubus.c
    spind_LDADD += -lubox -lubus -lblobmsg_json
endif

version.c:
	$(top_srcdir)/../scripts/create_version_c.sh $(top_srcdir)/../VERSION

.PHONY: version.c
