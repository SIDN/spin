AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g -Wall -Werror

bin_PROGRAMS = spin_webapi 
spin_webapi_SOURCES = spin_webapi.c \
                      traffic_capture.c \
                      rpc_client.c \
                      ../../spind/cJSON.c \
                      ../../lib/spindata_type.c


spin_webapi_CFLAGS = -DDATADIR='"$(datadir)"' -DSRCDIR='"$(srcdir)"'
spin_webapi_LDFLAGS = -lpthread

MYDIR = ${srcdir}
EXTRA_DIST = $(MYDIR)/templates $(MYDIR)/static

install-data-local:
	test -z $(pkgdatadir)/templates || $(MKDIR_P) $(pkgdatadir)/spin-webapi/templates
	cp -r "$(MYDIR)/templates" "$(pkgdatadir)/spin-webapi/"
	test -z $(pkgdatadir)/templates || $(MKDIR_P) $(pkgdatadir)/spin-webapi/static
	cp -r "$(MYDIR)/static" "$(pkgdatadir)/spin-webapi/"
