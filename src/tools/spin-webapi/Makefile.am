AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g -Wall -Werror

bin_PROGRAMS = spin_webapi 
spin_webapi_SOURCES = spin_webapi.c \
                      traffic_capture.c \
                      rpc_client.c \
                      ../../spind/cJSON.c \
                      ../../lib/spindata_type.c

