
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g -Wall -Werror

bin_PROGRAMS = spind

spind_SOURCES = spind.c \
                cJSON.c \
                config_common.c \
                config_uci.c \
                core2block.c \
                core2conntrack.c \
                core2extsrc.c \
                core2nflog_dns.c \
                core2pubsub.c \
                dnshooks.c \
                handle_command.h \
                mainloop.c \
                mainloop.h \
                nflogroutines.c \
                nfqroutines.c \
                rpc_calls.c \
                rpc_calls.h \
                rpc_common.c \
                rpc_json.c \
                spindata.c \
                spinhook.c \
                statistics.c


#spind_CFLAGS =
spind_LDADD = $(top_builddir)/lib/libspin.a

if USE_UBUS
    spind_SOURCES += rpc_ubus.c
    spind_LDADD += -lubox -lubus -lblobmsg_json
endif

version.c:
	$(top_srcdir)/../scripts/create_version_c.sh $(top_srcdir)/../VERSION

.PHONY: version.c
