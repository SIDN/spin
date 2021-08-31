AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g -Wall -Werror

bin_PROGRAMS = spinweb
spinweb_SOURCES = spinweb.c \
                  traffic_capture.c \
                  rpc_client.c \
                  files.c \
                  ../spind/cJSON.c


spinweb_CFLAGS = -DDATADIR='"$(datadir)"' -DSRCDIR='"$(srcdir)"'
spinweb_LDADD = -lpthread -lcrypt $(top_builddir)/lib/libspin.a

if USE_UBUS
    spinweb_SOURCES += rpc_ubus_client.c
    spinweb_LDADD += -lubox -lubus -lblobmsg_json
endif


MYDIR = ${srcdir}
EXTRA_DIST = $(MYDIR)/templates $(MYDIR)/static

install-data-local:
	test -z $(DESTDIR)$(pkgdatadir)/templates || $(MKDIR_P) $(DESTDIR)$(pkgdatadir)/spinweb/templates
	cp -r "$(MYDIR)/templates" "$(DESTDIR)$(pkgdatadir)/spinweb/"
	test -z $(DESTDIR)$(pkgdatadir)/templates || $(MKDIR_P) $(DESTDIR)$(pkgdatadir)/spinweb/static
	cp -r "$(MYDIR)/static" "$(DESTDIR)$(pkgdatadir)/spinweb/"
