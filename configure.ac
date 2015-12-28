# Process this file with autoconf to produce a configure script.
AC_PREREQ(2.57)
AC_INIT(mysqljsonexport, 1.2, anders@recordedfuture.com)
AM_INIT_AUTOMAKE(mysqljsonexport, 1.2)
AC_CONFIG_SRCDIR([mysqljsonexport.c])
AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([c], [_exit])
AC_CHECK_LIB([pthread], [pthread_mutex_init], , [Pthreads must be installed])
AC_CHECK_LIB([z], [compress])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([sys/utsname.h])
AC_CHECK_HEADERS([limits.h stdlib.h string.h termios.h unistd.h sys/types.h sys/stat.h pthread.h], [], AC_MSG_ERROR(Required header file $ac_header missing))

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_STRUCT_TM

# Handle MySQL configuration.
AC_ARG_WITH(mysql,
  [  --with-mysql            MySQL Directory ],
  [MYSQL_HOME=$withval
  ])

# Check if MySQL Home directory exists and has been specified.
AC_MSG_CHECKING(MySQL home directory)
if test "x$MYSQL_HOME" == "x" ; then
   MYSQL_EXEC=`which mysql 2> /dev/null`
   if test "x$MYSQL_EXEC" != "x" ; then
      MYSQL_DIR=`dirname $MYSQL_EXEC`
      if test "x$MYSQL_DIR" != "x" ; then
         MYSQL_HOME=`dirname $MYSQL_DIR`
         if test "x$MYSQL_HOME" != "x" -a -d "$MYSQL_HOME" ; then
            AC_MSG_RESULT($MYSQL_HOME)
         else
            AC_MSG_RESULT(no)
         fi
      else
         AC_MSG_RESULT(no)
      fi
   else
      AC_MSG_RESULT(no)
   fi
fi

# Check that we have a MySQL HOME
if test "x$MYSQL_HOME" == "x" ; then
   AC_MSG_ERROR(Can not find MySQL home directory)
else
   AC_MSG_NOTICE(Using MySQL Home: $MYSQL_HOME)
fi
AC_SUBST(MYSQL_HOME)

# Check for mysql_config.
AC_ARG_WITH(mysql_config,
  [  --with-mysql_config     Path to mysql_config program ],
  [ AC_MSG_CHECKING(for mysql_config)
   if test -x "$withval" ; then
     MYSQL_CONFIG=$withval
     AC_MSG_RESULT($MYSQL_CONFIG)
   else
     AC_MSG_RESULT(no)
     AC_MSG_ERROR([$withval doesnt exist or is not executable])
   fi
  ],
  [ if test "x$MYSQL_HOME" != "x" -a -x "$MYSQL_HOME/bin/mysql_config" ; then
     AC_MSG_CHECKING(for mysql_config)
     MYSQL_CONFIG="$MYSQL_HOME/bin/mysql_config"
     AC_MSG_RESULT($MYSQL_CONFIG)
  else
     AC_CHECK_PROG(MYSQL_CONFIG, mysql_config, mysql_config)
  fi
  ])

# If mysql_config exists, then figure out some flags from it.
if test -x $MYSQL_CONFIG ; then
  CPPFLAGS="$CPPFLAGS `$MYSQL_CONFIG --cflags`"
  LDFLAGS="$LDFLAGS `$MYSQL_CONFIG --libs_r`"
  MYSQL_VERSION=`$MYSQL_CONFIG --version`
else
  MYSQL_VERSION="unknown"
fi
AC_DEFINE_UNQUOTED([MYSQL_VERSION], "$MYSQL_VERSION", "MySQL Version for building")

# If MYSQL_HOME is found, then add it to compiler and linker flags.
if test "x$MYSQL_HOME" != "x" -a -d "$MYSQL_HOME" ; then
  CPPFLAGS="$CPPFLAGS -I$MYSQL_HOME/include -I$MYSQL_HOME/include/mysql"
  LDFLAGS="$LDFLAGS -L$MYSQL_HOME/lib -L$MYSQL_HOME/lib/mysql"
fi

# MySQL Specific LDFLAGS
AC_ARG_WITH(mysql-ldflags,
  [  --with-mysql-ldflags    MySQL ldflags ],
  [ MYSQLLDFLAGS="$withval" ],
  [ MYSQLLDFLAGS="-lmysqlclient_r" ])
LDFLAGS="$LDFLAGS $MYSQLLDFLAGS"

# MySQL Specific CFLAGS
AC_ARG_WITH(mysql-cflags,
  [  --with-mysql-cflags     MySQL cflags ],
  [ CPPFLAGS="$CPPFLAGS $withval" ]
  )

# Construct archive name to use for binary distribution
GZ_BINARCHIVE_NAME="$PACKAGE_NAME-$PACKAGE_VERSION-$host_vendor-$host_os-$build_cpu-$MYSQL_VERSION.tgz"
AC_SUBST(GZ_BINARCHIVE_NAME)

AC_ARG_WITH(pthread,
  [  --with-pthread          Pthread library ],
  [PTHREADLIB=$withval ])
AC_SUBST(PTHREADLIB)

# Check for diff program.
AC_CHECK_PROG(DIFF, diff, diff)
AC_SUBST(DIFF)

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_TYPE_SIGNAL

AC_CONFIG_FILES([Makefile tests/Makefile])
AC_OUTPUT
