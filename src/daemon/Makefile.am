noinst_LTLIBRARIES        = libsnra_daemon.la

libsnra_daemon_la_SOURCES = \
    snra-avahi.c \
    snra-avahi.h \
    snra-config.c \
    snra-config.h \
    snra-http-resource.c \
    snra-http-resource.h \
    snra-manager.c \
    snra-manager.h \
    snra-media-db.c \
    snra-media-db.h \
    snra-resource.c \
    snra-resource.h \
    snra-server.c \
    snra-server.h \
    snra-server-client.c \
    snra-server-client.h 
libsnra_daemon_la_CPPFLAGS = -I$(top_srcdir) $(SNRA_COMMON_CFLAGS) $(GST_CFLAGS) $(GST_RTSP_CFLAGS) $(EXTRA_CFLAGS)
libsnra_daemon_la_LIBADD  = 
