dnl AM_PATH_GCONF([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND [, MODULES]]]])
dnl Test for GCONF, and define GCONF_CFLAGS and GCONF_LIBS
dnl
AC_DEFUN(AM_PATH_GCONF,
[dnl 
dnl Get the cflags and libraries from the gconf-config script
dnl
AC_ARG_WITH(gconf-prefix,[  --with-gconf-prefix=PFX   Prefix where GCONF is installed (optional)],
            gconf_config_prefix="$withval", gconf_config_prefix="")
AC_ARG_WITH(gconf-exec-prefix,[  --with-gconf-exec-prefix=PFX Exec prefix where GCONF is installed (optional)],
            gconf_config_exec_prefix="$withval", gconf_config_exec_prefix="")
AC_ARG_ENABLE(gconftest, [  --disable-gconftest       Do not try to compile and run a test GCONF program],
		    , enable_gconftest=yes)

  gconf_config_args="$gconf_config_args $4"

  if test x$gconf_config_exec_prefix != x ; then
     gconf_config_args="$gconf_config_args --exec-prefix=$gconf_config_exec_prefix"
     if test x${GCONF_CONFIG+set} != xset ; then
        GCONF_CONFIG=$gconf_config_exec_prefix/bin/gconf-config
     fi
  fi
  if test x$gconf_config_prefix != x ; then
     gconf_config_args="$gconf_config_args --prefix=$gconf_config_prefix"
     if test x${GCONF_CONFIG+set} != xset ; then
        GCONF_CONFIG=$gconf_config_prefix/bin/gconf-config
     fi
  fi

  AC_PATH_PROG(GCONF_CONFIG, gconf-config, no)
  min_gconf_version=ifelse([$1], , 0.3, $1)
  AC_MSG_CHECKING(for GCONF - version >= $min_gconf_version)
  no_gconf=""
  if test "$GCONF_CONFIG" = "no" ; then
    no_gconf=yes
  else
    GCONF_CFLAGS="`$GCONF_CONFIG $gconf_config_args --cflags`"
    GCONF_LIBS="`$GCONF_CONFIG $gconf_config_args --libs`"
    gconf_config_major_version=`$GCONF_CONFIG $gconf_config_args --version | \
	   sed -e 's,^[[^0-9.]]*,,g' -e 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\1/'`
    gconf_config_minor_version=`$GCONF_CONFIG $gconf_config_args --version | \
	   sed -e 's,^[[^0-9.]]*,,g' -e 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\2/'`
    gconf_config_micro_version=`$GCONF_CONFIG $gconf_config_args --version | \
	   sed -e 's,^[[^0-9\.]]*,,g' -e 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\3/'`
  fi
  if test "x$no_gconf" = x ; then
     AC_MSG_RESULT(yes)
     ifelse([$2], , :, [$2])     
  else
     AC_MSG_RESULT(no)
     if test "$GCONF_CONFIG" = "no" ; then
       echo "*** The gconf-config script installed by GCONF could not be found"
       echo "*** If GCONF was installed in PREFIX, make sure PREFIX/bin is in"
       echo "*** your path, or set the GCONF_CONFIG environment variable to the"
       echo "*** full path to gconf-config."
     else
	:
     fi
     GCONF_CFLAGS=""
     GCONF_LIBS=""
     ifelse([$3], , :, [$3])
  fi
  AC_SUBST(GCONF_CFLAGS)
  AC_SUBST(GCONF_LIBS)
  rm -f conf.gconftest
])
