dnl Process this file with autoconf to produce a configure script.
AC_INIT([spin], [0.6.0])
AC_PREREQ([2.50])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([-Wall foreign tar-pax no-dist-gzip dist-bzip2 1.10b subdir-objects])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
dnl kernel style compile messages
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl Checks for programs.
AC_PROG_MAKE_SET
AC_PROG_CC
AC_PROG_INSTALL

dnl Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS(fcntl.h unistd.h)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_STRUCT_TM
AC_SYS_LARGEFILE

dnl Checks for library functions.
AC_FUNC_VPRINTF
AC_CHECK_FUNCS(socket strerror)

AC_PROG_RANLIB

regular_CFLAGS="-Wall -Wextra -Wno-unused-parameter -Werror -O0"
AC_SUBST([regular_CFLAGS])

dnl Check for the right nfnetlink version
PKG_CHECK_MODULES([libnfnetlink], [libnfnetlink >= 1.0.1])

AC_CHECK_LIB([mosquitto], [mosquitto_lib_version], [], [AC_MSG_ERROR([libmosquitto not found])])

#AC_CHECK_LIB([netfilter-log], [XXXXXXXX], [], [AC_MSG_ERROR([libXXXXXXX not found])])
AC_CHECK_LIB([mnl], [mnl_socket_open], [LDFLAGS="$LDFLAGS -lmnl"], [AC_MSG_ERROR([libmnl not found])])
AC_CHECK_LIB([netfilter_log], [nflog_open], [LDFLAGS="$LDFLAGS -lnetfilter_log"], [AC_MSG_ERROR([libnetfilter-log not found])])
AC_CHECK_LIB([netfilter_queue], [nfq_open], [LDFLAGS="$LDFLAGS -lnetfilter_queue"], [AC_MSG_ERROR([libnetfilter-queue not found])])
AC_CHECK_LIB([netfilter_conntrack], [nfct_new], [LDFLAGS="$LDFLAGS -lnetfilter_conntrack"], [AC_MSG_ERROR([libnetfilter-conntrack not found])])

AC_CHECK_LIB([ldns], [ldns_wire2pkt], [LDFLAGS="$LDFLAGS -lldns"], [AC_MSG_ERROR([ldns not found])])



# In case of UCI, check if it is available, and use it if so
# no problem if not (we'll use standard config
AC_CHECK_LIB([uci], [uci_alloc_context],
    [
        AC_DEFINE([USE_UCI], [1], [Use UCI for configuration])
        LDFLAGS="$LDFLAGS -luci"
    ],
    [
        AC_DEFINE([USE_UCI], [0], [Do not use UCI for configuration])
    ])

AC_CHECK_LIB([ubus], [ubus_connect],
    [
        use_ubus=1
        AC_DEFINE([USE_UBUS], [1], [Use UBUS for RPC])
    ],
    [
        use_ubus=0
    ])
AM_CONDITIONAL([USE_UBUS],  [test "x$use_ubus" = "x1"])

AC_ARG_WITH(linux-headers,
    AC_HELP_STRING([--with-linux-headers=PATH], [specify path of linux headers]),
    [ AC_SUBST(KERNELPATH, "$withval") ]
)

MAKEFILES="Makefile src/Makefile"

AC_ARG_ENABLE(tests,
              AC_HELP_STRING([--enable-tests], [enable unit tests and code coverage]),
              [
                MAKEFILES="$MAKEFILES lib/tests/Makefile";
                AC_SUBST(TESTDIR, "tests")
                AC_CONFIG_FILES(lib/tests/Makefile)
                CFLAGS="-g -O0"
              ], [])


AC_CONFIG_FILES(
    Makefile
    lib/Makefile
    spind/Makefile
    tools/spin_print/Makefile
    tools/spin_config/Makefile
)
AC_OUTPUT
