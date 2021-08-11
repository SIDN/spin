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
AC_CHECK_HEADERS(fcntl.h net/ethernet.h unistd.h)
AC_CHECK_HEADERS(netinet/if_ether.h, [], [],
[
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
])

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_STRUCT_TM
AC_SYS_LARGEFILE
AC_CHECK_HEADER([net/bpf.h], AC_CHECK_TYPE([struct bpf_timeval],
  AC_DEFINE([HAVE_BPF_TIMEVAL], [1], [struct bpf_timeval exists]), [], [[
#include <sys/types.h>
#include <net/bpf.h>
]]))

dnl Checks for library functions.
AC_FUNC_VPRINTF
AC_CHECK_FUNCS(pledge socket strerror)

AC_PROG_RANLIB

regular_CFLAGS="-Wall -Wextra -Wno-unused-parameter -Werror -O0"
AC_SUBST([regular_CFLAGS])

AC_CHECK_LIB([mosquitto], [mosquitto_lib_version], [], [AC_MSG_ERROR([libmosquitto not found])])
AC_CHECK_LIB([microhttpd], [MHD_start_daemon], [], [AC_MSG_ERROR([libmicrohttpd not found])])
AC_CHECK_LIB([c], [crypt], [], AC_CHECK_LIB([crypt], [crypt], [], [AC_MSG_ERROR([libcrypt not found])]))

passivemodeonly=0
AC_ARG_ENABLE([passive-mode-only],
              AC_HELP_STRING([--enable-passive-mode-only], [only allow spind to run in passive mode (i.e. do not rely on the Netlink and Netfilter APIs)]),
              [
                if test "x$enableval" = "xyes" ; then
                  passivemodeonly=1
                fi
              ])
AM_CONDITIONAL([PASSIVE_MODE_ONLY],  [test "x$passivemodeonly" = "x1"])
if test "x$passivemodeonly" = "x1" ; then
    AC_DEFINE([PASSIVE_MODE_ONLY], [1], [Enable passive mode only])
fi

#AC_CHECK_LIB([netfilter-log], [XXXXXXXX], [], [AC_MSG_ERROR([libXXXXXXX not found])])
AC_CHECK_LIB([mnl], [mnl_socket_open], [],
    [
        if test "x$passivemodeonly" != "x1" ; then
            AC_MSG_ERROR([libmnl not found])
        fi
    ])
AC_CHECK_LIB([netfilter_log], [nflog_open], [],
    [
        if test "x$passivemodeonly" != "x1" ; then
            AC_MSG_ERROR([libnetfilter-log not found])
        fi
    ])
AC_CHECK_LIB([netfilter_queue], [nfq_open], [],
    [
        if test "x$passivemodeonly" != "x1" ; then
            AC_MSG_ERROR([libnetfilter-queue not found])
        fi
    ])
AC_CHECK_LIB([netfilter_conntrack], [nfct_new], [],
    [
        if test "x$passivemodeonly" != "x1" ; then
            AC_MSG_ERROR([libnetfilter-conntrack not found])
        fi
    ])

AC_CHECK_LIB([ldns], [ldns_wire2pkt], [], [AC_MSG_ERROR([ldns not found])])



# In case of UCI, check if it is available, and use it if so
# no problem if not (we'll use standard config
AC_CHECK_LIB([uci], [uci_alloc_context],
    [
        AC_DEFINE([USE_UCI], [1], [Use UCI for configuration])
        LIBS="$LIBS -luci"
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

MAKEFILES="Makefile src/Makefile spinweb/Makefile"

AC_ARG_ENABLE(spin-pcap-reader,
              AC_HELP_STRING([--enable-spin-pcap-reader], [enable spin-pcap-reader]),
              [
                MAKEFILES="$MAKEFILES tools/spin-pcap-reader/Makefile";
                AC_SUBST(SPINPCAPREADER, "spin-pcap-reader")
                AC_CONFIG_FILES(tools/spin-pcap-reader/Makefile)
                CFLAGS="$CFLAGS -g -O0"
                AC_CHECK_LIB([pcap], [pcap_create], [], [AC_MSG_ERROR([pcap not found])])
              ], [])

AC_ARG_ENABLE(tests,
              AC_HELP_STRING([--enable-tests], [enable unit tests and code coverage]),
              [
                MAKEFILES="$MAKEFILES lib/tests/Makefile";
                AC_SUBST(TESTDIR, "tests")
                AC_CONFIG_FILES(lib/tests/Makefile)
                CFLAGS="$CFLAGS -g -O0"
              ], [])


AC_CONFIG_FILES(
    Makefile
    lib/Makefile
    spind/Makefile
    spinweb/Makefile
    tools/Makefile
)
AC_OUTPUT
