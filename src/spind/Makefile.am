
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g -Wall -Werror

bin_PROGRAMS = spind

spind_SOURCES = spind.c \
                cJSON.c \
                config_common.c \
                config_uci.c \
                core2block.c \
                core2extsrc.c \
                core2pubsub.c \
                dots.c \
                dots.h \
                dnshooks.c \
                mainloop.c \
                mainloop.h \
                rpc_calls.c \
                rpc_calls.h \
                rpc_common.c \
                rpc_json.c \
                spindata.c \
                spinhook.c \
                statistics.c
if !PASSIVE_MODE_ONLY
spind_SOURCES += core2conntrack.c \
                 core2nflog_dns.c \
                 nflogroutines.c \
                 nfqroutines.c
endif


#spind_CFLAGS =
spind_LDADD = $(top_builddir)/lib/libspin.a

if USE_UBUS
    spind_SOURCES += rpc_ubus.c
    spind_LDADD += -lubox -lubus -lblobmsg_json
endif

version.c:
	$(top_srcdir)/../scripts/create_version_c.sh $(top_srcdir)/../VERSION

.PHONY: version.c
