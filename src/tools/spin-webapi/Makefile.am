AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g -Wall -Werror

bin_PROGRAMS = spin_webapi 
spin_webapi_SOURCES = spin_webapi.c \
                      traffic_capture.c \
                      rpc_client.c \
                      ../../spind/cJSON.c \
                      ../../lib/spindata_type.c


spin_webapi_CFLAGS = -DDATADIR='"$(datadir)"' -DSRCDIR='"$(abs_srcdir)"'
spin_webapi_LDFLAGS = -lpthread

#spin_wepabi_templatesdir = $(prefix)/tools/spin-webapi/templates
#spin_wepabi_templates_DATA = templates/
spin_webapi_templatesdir = $(datadir)/spin-webapi/templates
spin_webapi_templates_DATA = templates/capture.html

MYDIR = ${srcdir}
EXTRA_DIST = $(MYDIR)

install-data-local:
	test -z $(pkgdatadir)/templates || $(MKDIR_P) $(pkgdatadir)/webapi/templates
	find "$(MYDIR)/templates" -type f -exec $(INSTALL_DATA) {} $(pkgdatadir)/webapi/templates \;
	test -z $(pkgdatadir)/templates || $(MKDIR_P) $(pkgdatadir)/webapi/static
	find "$(MYDIR)/static" -type f -exec $(INSTALL_DATA) {} $(pkgdatadir)/webapi/static \;
