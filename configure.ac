AC_PREREQ(2.57)

AC_INIT([glxcompmgr], [0.0.1], [davidr@novell.com])

AC_CONFIG_AUX_DIR(config)

AM_INIT_AUTOMAKE([dist-bzip2])
AC_CONFIG_HEADER([config.h])
AM_MAINTAINER_MODE

AC_ISC_POSIX
AC_PROG_CC
AC_PROG_CPP
AC_PROG_LIBTOOL
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h sys/time.h unistd.h])

if test "x$GCC" = "xyes"; then
  case " $CFLAGS " in
  *[[\ \	]]-Wall[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wall" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-Wpointer-arith[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wpointer-arith" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-Wstrict-prototypes[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wstrict-prototypes" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-Wmissing-prototypes[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wmissing-prototypes" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-Wmissing-declarations[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wmissing-declarations" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-Wnested-externs[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wnested-externs" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-fno-strict-aliasing[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -fno-strict-aliasing" ;;
  esac

  if test "x$enable_ansi" = "xyes"; then
    case " $CFLAGS " in
    *[[\ \	]]-ansi[[\ \	]]*) ;;
    *) CFLAGS="$CFLAGS -ansi" ;;
    esac

    case " $CFLAGS " in
    *[[\ \	]]-pedantic[[\ \	]]*) ;;
    *) CFLAGS="$CFLAGS -pedantic" ;;
    esac
  fi
fi

AC_C_BIGENDIAN

plugindir=$libdir/glxcomp
AC_SUBST(plugindir)

imagedir=$datadir/glxcomp
AC_SUBST(imagedir)

GLXCOMP_REQUIRES="libpng xcomposite xfixes xdamage"
PKG_CHECK_MODULES(GLXCOMP, $GLXCOMP_REQUIRES)
AC_SUBST(GLXCOMP_REQUIRES)

AC_MSG_CHECKING(for GL_CFLAGS)
AC_ARG_WITH(gl-cflags, [  --with-gl-cflags=CFLAGS ],
		       [GL_CFLAGS="$withval"],
		       [GL_CFLAGS=""])

AC_MSG_RESULT($GL_CFLAGS)
AC_MSG_CHECKING(for GL_LIBS)
AC_ARG_WITH(gl-libs, [  --with-gl-libs=LIBS ],
		     [GL_LIBS="$withval"],
		     [GL_LIBS="-lGL"])
AC_MSG_RESULT($GL_LIBS)

AC_SUBST(GL_CFLAGS)
AC_SUBST(GL_LIBS)

AC_ARG_ENABLE(gconf,
  [  --disable-gconf         Disable gconf plugin],
  [use_gconf=$enableval], [use_gconf=yes])

if test "x$use_gconf" = "xyes"; then
  PKG_CHECK_MODULES(GCONF, gconf-2.0, [use_gconf=yes], [use_gconf=no])
fi

AM_CONDITIONAL(GCONF_PLUGIN, test "x$use_gconf" = "xyes")
if test "$use_gconf" = yes; then
  AC_DEFINE(USE_GCONF, 1, [Build gconf plugin])
fi

AC_ARG_ENABLE(libsvg-cairo,
  [  --enable-libsvg-cairo   Enable svg support],
  [use_libsvg_cairo=$enableval], [use_libsvg_cairo=no])

if test "x$use_libsvg_cairo" = "xyes"; then
  PKG_CHECK_MODULES(LIBSVG_CAIRO, libsvg-cairo, 
				  [use_libsvg_cairo=yes], 
				  [use_libsvg_cairo=no])
fi

AM_CONDITIONAL(USE_LIBSVG_CAIRO, [test x$use_libsvg_cairo = xyes])
if test "$use_libsvg_cairo" = yes; then
  AC_DEFINE(USE_LIBSVG_CAIRO, 1, [libsvg-cairo for SVG support])
fi

AC_OUTPUT([
glxcomp.pc
Makefile
src/Makefile
include/Makefile
plugins/Makefile
images/Makefile
])

echo ""
echo "the following optional plugins will be compiled:"
echo "  gconf: $use_gconf"
echo ""
echo "and the following optional features will be compiled:"
echo "  svg:   $use_libsvg_cairo"
echo ""
