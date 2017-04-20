
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g

lib_LTLIBRARIES = lua-spin-nflog.la

lua_spin_nflog_la_SOURCES = lua-spin-nflog.c
lua_spin_nflog_la_LDFLAGS = -module -lnetfilter_log -lnfnetlink
lua_spin_nflog_la_CFLAGS = ${LUA_INCLUDE}
lua_spin_nflog_la_LIBADD = ${LUA_LIB} -lldns
