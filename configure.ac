AC_PREREQ([2.68])
AC_INIT([aurena], [0.9], [thaytan@noraisin.net])

AM_INIT_AUTOMAKE([-Wno-portability 1.11 no-dist-gzip dist-bzip2 dist-xz tar-ustar subdir-objects])

AM_SILENT_RULES([yes])

AM_MAINTAINER_MODE([enable])

AC_CONFIG_SRCDIR([src/aurena-server.c])
AM_CONFIG_HEADER([config.h])

AC_CANONICAL_HOST

dnl Find C compiler with C99
AC_PROG_CC
AC_PROG_CC_STDC

LT_INIT

PKG_PROG_PKG_CONFIG([0.22])
PKG_CHECK_MODULES(AUR_COMMON, [gobject-2.0 glib-2.0 >= 2.30 gio-2.0 avahi-client avahi-glib >= 0.6.24 json-glib-1.0 libsoup-2.4 >= 2.42])
AC_DEFINE([HAVE_AVAHI], 1, [Defined if compiling with Avahi support])

AC_MSG_NOTICE([Checking for GStreamer 1.0])
PKG_CHECK_MODULES(GST_1_0, [gstreamer-1.0 gstreamer-net-1.0 gstreamer-tag-1.0 gstreamer-audio-1.0], [HAVE_GST_1_0=yes], [HAVE_GST_1_0=no])
if test "x$HAVE_GST_1_0" = "xyes"; then
  GST_CFLAGS="$GST_1_0_CFLAGS -DGST_USE_UNSTABLE_API"
  GST_LIBS="$GST_1_0_LIBS"
else
  AC_MSG_ERROR([GStreamer 1.0 required])
fi

PKG_CHECK_MODULES(GST_RTSP, [gstreamer-rtsp-server-1.0], [HAVE_GST_RTSP=yes], [HAVE_GST_RTSP=no])
if test "x$HAVE_GST_RTSP" != "xyes"; then
  AC_MSG_ERROR([gst-rtsp-server-1.0 required])
fi

AC_MSG_NOTICE([Check for GTK+ 3.0 requirements])
PKG_CHECK_MODULES(GTKAPPS, [gtk+-3.0], [BUILD_GTKAPPS=yes],
		        [BUILDS_GTKAPP=no])
AM_CONDITIONAL(BUILD_GTKAPPS, test "x$BUILD_GTKAPPS" = "xyes")
if test "x$BUILD_GTKAPPS" = "xyes"; then
   if test "x$HAVE_GST_1_0" = "xyes"; then
      GTKAPPS_LIBS="$GTKAPPS_LIBS -lgstvideo-1.0"
   else
      GTKAPPS_LIBS="$GTKAPPS_LIBS -lgstinterfaces-0.10"
   fi
   AC_SUBST([GTKAPP_CFLAGS])
   AC_SUBST([GTKAPPS_LIBS])
fi

AC_MSG_NOTICE([Checking whether to build server (sqlite >= 3.3, glib >= 2.32)])
PKG_CHECK_MODULES(AUR_SERVER, [sqlite3 >= 3.3 glib-2.0 >= 2.32 libsoup-2.4 > 2.48 ],
    [BUILD_AUR_SERVER=yes], [BUILD_AUR_SERVER=no])
AM_CONDITIONAL(BUILD_AUR_SERVER, test "x$BUILD_AUR_SERVER" = "xyes")

if test "x$HAVE_GST_RTSP" = "xyes"; then
  AC_DEFINE([HAVE_GST_RTSP], 1, [Define if gstreamer-rtsp-server is available])
fi

AC_SUBST([GST_CFLAGS])
AC_SUBST([GST_LIBS])

EXTRA_CFLAGS="-Wall -Wextra -Wno-cast-function-type"
AC_SUBST([EXTRA_CFLAGS])

AC_CONFIG_FILES([
  Makefile
  src/Makefile
  src/common/Makefile
  src/client/Makefile
  src/server/Makefile
  data/Makefile
  data/htdocs/Makefile
  test/Makefile
  test/mic/Makefile
])
AC_OUTPUT
