dnl Process this file with autoconf to produce a configure script.
AC_INIT([spin], [0.2.0])
AC_PREREQ([2.50])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([-Wall foreign tar-pax no-dist-gzip dist-bzip2 1.10b])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
dnl kernel style compile messages
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl Checks for programs.
AC_PROG_MAKE_SET
AC_PROG_CC
AC_PROG_INSTALL
AC_DISABLE_STATIC
AC_PROG_LIBTOOL

dnl Checks for libraries.
AC_SEARCH_LIBS([dlopen], [dl], [libdl_LIBS="$LIBS"; LIBS=""])
AC_SUBST([libdl_LIBS])

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

regular_CFLAGS="-Wall -Wextra -Wno-unused-parameter"
AC_SUBST([regular_CFLAGS])

#AC_SEARCH_LIBS([pthread_create], [pthread], [libpthread_LIBS="$LIBS"; LIBS=""])
#AC_SUBST([libpthread_LIBS])

#AC_ARG_ENABLE(ulog,
#       AS_HELP_STRING([--enable-ulog], [Enable ulog module [default=yes]]),[enable_ulog=$enableval],[enable_ulog=yes])
#AM_CONDITIONAL([BUILD_ULOG], [test "x$enable_ulog" = "xyes"])
#if [! test "x$enable_ulog" = "xyes"]; then
#	enable_ulog="no"
#fi

dnl Check for the right nfnetlink version
PKG_CHECK_MODULES([LIBNFNETLINK], [libnfnetlink >= 1.0.1])
AC_ARG_ENABLE(nflog,
       AS_HELP_STRING([--enable-nflog], [Enable nflog module [default=yes]]),[enable_nflog=$enableval],[enable_nflog=yes])
AS_IF([test "x$enable_nflog" = "xyes"], [
    PKG_CHECK_MODULES([LIBNETFILTER_LOG], [libnetfilter_log >= 1.0.0])
    AC_DEFINE([BUILD_NFLOG], [1], [Building nflog module])
])
AM_CONDITIONAL([BUILD_NFLOG], [test "x$enable_nflog" = "xyes"])
if [! test "x$enable_nflog" = "xyes"]; then
	enable_nflog="no"
fi

#AC_ARG_ENABLE(nfct,
#       AS_HELP_STRING([--enable-nfct], [Enable nfct module [default=yes]]),[enable_nfct=$enableval],[enable_nfct=yes])
#AS_IF([test "x$enable_nfct" = "xyes"], [
#    PKG_CHECK_MODULES([LIBNETFILTER_CONNTRACK], [libnetfilter_conntrack >= 1.0.2])
#    AC_DEFINE([BUILD_NFCT], [1], [Building nfct module])
#])
#AM_CONDITIONAL([BUILD_NFCT], [test "x$enable_nfct" = "xyes"])
#if [! test "x$enable_nfct" = "xyes"]; then
#	enable_nfct="no"
#fi

#AC_ARG_ENABLE(nfacct,
#       AS_HELP_STRING([--enable-nfacct], [Enable nfacct module [default=yes]]),[enable_nfacct=$enableval],[enable_nfacct=yes])
#AS_IF([test "x$enable_nfacct" = "xyes"], [
#    PKG_CHECK_MODULES([LIBMNL], [libmnl >= 1.0.3])
#    PKG_CHECK_MODULES([LIBNETFILTER_ACCT], [libnetfilter_acct >= 1.0.1])
#    AC_DEFINE([BUILD_NFACCT], [1], [Building nfacct module])
#])
#AM_CONDITIONAL([BUILD_NFACCT], [test "x$enable_nfacct" = "xyes"])
#if [! test "x$enable_nfacct" = "xyes"]; then
#	enable_nfacct="no"
#fi

AC_CONFIG_FILES(src/Makefile)
AC_OUTPUT

echo "
SPIN Controller configuration:
  Input plugins:
    NFLOG plugin:			${enable_nflog}
"
echo "You can now run 'make' and 'make install'"
