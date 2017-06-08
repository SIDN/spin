
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g

lib_LTLIBRARIES = lua-spin_nflog.la

lua_spin_nflog_la_SOURCES = lua-spin_nflog.c
lua_spin_nflog_la_LDFLAGS = -module -lnetfilter_log -lnfnetlink
lua_spin_nflog_la_CFLAGS = ${LUA_INCLUDE}
lua_spin_nflog_la_LIBADD = ${LUA_LIB} -lldns

bin_PROGRAMS = spin_config spin_print
spin_config_SOURCES = spin_config.c
spin_config_CFLAGS = -I../kernelmod
spin_config_LDADD = -L../kernelmod

spin_print_SOURCES = spin_print.c ../kernelmod/pkt_info.c
spin_print_CFLAGS = -I../kernelmod
spin_print_LDADD = -L../kernelmod
