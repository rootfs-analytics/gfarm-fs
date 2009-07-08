#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([gfarm_gridftp_dsi], [1.0])
AC_CONFIG_SRCDIR([globus_gridftp_server_gfarm.c])
AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_AWK
AC_PROG_CC

### --with-gfarm=GFARM
AC_MSG_CHECKING([Gfarm file system package])
AC_ARG_WITH([gfarm],
  AC_HELP_STRING([--with-gfarm=PATH], [path to gfarm file system [[/usr]]]))
case x$with_gfarm in
  xyes|xno) GFARM=/usr ;;
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
AC_MSG_CHECKING([Globus toolkit])
AC_ARG_WITH([globus],
  AC_HELP_STRING([--with-globus=PATH], [path to Globus toolkit [[$GLOBUS_LOCATION]]]))
case x$with_globus in
  xyes|xno) GLOBUS_LOCATION=$GLOBUS_LOCATION ;;
  *) GLOBUS_LOCATION=$with_globus ;;
esac
if test x"$GLOBUS_LOCATION" = x; then
  AC_MSG_ERROR([GLOBUS_LOCATION is not specified.  Use --with-globus option.])
fi
AC_MSG_RESULT([$GLOBUS_LOCATION])
if test ! -f $GLOBUS_LOCATION/etc/globus-user-env.sh; then
  AC_MSG_ERROR([$GLOBUS_LOCATION/etc/globus-user-env.sh not found.  Use --with-globus option.])
fi
export GLOBUS_LOCATION
. $GLOBUS_LOCATION/etc/globus-user-env.sh  

# guess FLAVOR
FTPSERVER=${GLOBUS_LOCATION}/sbin/globus-gridftp-server
if test ! -f $FTPSERVER; then
  AC_MSG_ERROR([$FTPSERVER is not found.])
fi
if test x"$FLAVOR" = x; then
    FLAVOR=`ldd ${FTPSERVER} | $AWK '($1 ~ /^libglobus_gridftp_server_control_/){name=gensub(/^libglobus_gridftp_server_control_(.+)\.so.*/,"\\\\1","g",$1);print name}'`
fi
if test x"$FLAVOR" = x; then
    AC_MSG_ERROR([Specify Globus flavor with --with-globus-flavor option.])
fi

AC_SUBST(GLOBUS_LOCATION)
AC_SUBST(FLAVOR)
AC_SUBST(GFARM)

# make sure nonthreaded gridftp server is installed
f_prefix=`expr "${FLAVOR}" : '\(.*\)pthr'`
f_pthr=`expr "${FLAVOR}" : '.*\(pthr\)'`
if test x"$f_pthr" = xpthr; then
  AC_MSG_ERROR([Copy ${GLOBUS_LOCATION}/sbin/${f_prefix}/shared/globus-gridftp-server into ${GLOBUS_LOCATION}/sbin/globus-gridftp-server.])
fi

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

# create $HEADER file
HEADER=globus_makefile_header
if test ! -s $HEADER; then
    ${GLOBUS_LOCATION}/bin/globus-makefile-header \
	-flavor=${FLAVOR} globus_gridftp_server > $HEADER
fi
if test ! -s $HEADER; then
    AC_MSG_ERROR([cannot create ${HEADER}.])
fi

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
