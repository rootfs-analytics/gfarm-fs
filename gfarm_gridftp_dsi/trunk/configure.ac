#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([gfarm_gridftp_dsi], [1.0.0])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_SRCDIR([globus_gridftp_server_gfarm.c])
AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL

### --with-gfarm=GFARM
AC_MSG_CHECKING([for Gfarm file system])
AC_ARG_WITH([gfarm],
  AC_HELP_STRING([--with-gfarm=PATH], [path to gfarm file system [[/usr]]]))
case x$with_gfarm in
  x|xyes|xno) GFARM=/usr ;;
  *) GFARM=$with_gfarm ;;
esac
if test x"$GFARM" != x/usr; then
  CPPFLAGS="$CPPFLAGS -I$GFARM/include"
  LDFLAGS="$LDFLAGS -R$GFARM/lib -L$GFARM/lib"
fi
AC_MSG_RESULT([$GFARM])

### --with-globus-flavor=FLAVOR
AC_ARG_WITH(globus_flavor,
[  --with-globus-flavor=FLAVOR	globus flavor name
				[[default=guessed]]],
[FLAVOR="${withval}"
],
[FLAVOR=
])

### --with-globus=GLOBUS_LOCATION
AC_MSG_CHECKING([for Globus toolkit])
AC_ARG_WITH([globus],
  AC_HELP_STRING([--with-globus=PATH], [path to Globus toolkit [[$GLOBUS_LOCATION]]]))
case x$with_globus in
  x|xyes|xno) GLOBUS_LOCATION=$GLOBUS_LOCATION ;;
  *) GLOBUS_LOCATION=$with_globus ;;
esac
if test x"$GLOBUS_LOCATION" = x; then
  AC_MSG_RESULT([no])
  AC_MSG_ERROR([Specify GLOBUS_LOCATION using --with-globus option])
fi
AC_MSG_RESULT([$GLOBUS_LOCATION])

# guess FLAVOR
AC_MSG_CHECKING([for Globus flavor])
if test x"$FLAVOR" = x; then
  FTPSERVER=$GLOBUS_LOCATION/sbin/globus-gridftp-server
  FLAVOR=`ldd $FTPSERVER | $AWK '($1 ~ /^libglobus_gridftp_server_control_/){name=gensub(/^libglobus_gridftp_server_control_(.+)\.so.*/,"\\\\1","g",$1);print name}'`
fi
if test x"$FLAVOR" = x; then
  AC_MSG_RESULT([no])
  AC_MSG_ERROR([Specify Globus flavor using --with-globus-flavor option])
fi
AC_MSG_RESULT([$FLAVOR])
AC_SUBST(FLAVOR)
if test x"$GLOBUS_LOCATION" != x/usr; then
  CPPFLAGS="$CPPFLAGS -I$GLOBUS_LOCATION/include/$FLAVOR"
  LDFLAGS="$LDFLAGS -R$GLOBUS_LOCATION/lib -L$GLOBUS_LOCATION/lib"
fi

# make sure nonthreaded gridftp server is installed
AC_MSG_CHECKING([whether nonthreaded GridFTP server is installed])
f_prefix=`expr "$FLAVOR" : '\(.*\)pthr'`
f_pthr=`expr "$FLAVOR" : '.*\(pthr\)'`
if test x"$f_pthr" = xpthr; then
  AC_MSG_RESULT([no])
  AC_MSG_ERROR([Copy $GLOBUS_LOCATION/sbin/$f_prefix/shared/globus-gridftp-server to $GLOBUS_LOCATION/sbin/globus-gridftp-server])
fi
AC_MSG_RESULT([yes])

# Checks for libraries.
AC_CHECK_LIB([gfarm], [gfarm_initialize],, [AC_MSG_ERROR([libgfarm not found.  Use --with-gfarm option])])

# Checks for header files.
AC_CHECK_HEADERS([gfarm/gfarm.h],, [AC_MSG_ERROR([gfarm/gfarm.h not found])])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_MODE_T
AC_TYPE_UID_T

# Checks for library functions.
AC_HEADER_STDC
AC_CHECK_FUNCS([memset strdup])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
