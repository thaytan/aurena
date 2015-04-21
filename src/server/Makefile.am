noinst_LTLIBRARIES        = libaurena_server.la

libaurena_server_la_SOURCES = \
    aur-avahi.c \
    aur-avahi.h \
    aur-config.c \
    aur-config.h \
    aur-http-resource.c \
    aur-http-resource.h \
    aur-manager.c \
    aur-manager.h \
    aur-media-db.c \
    aur-media-db.h \
    aur-resource.c \
    aur-resource.h \
    aur-server.c \
    aur-server.h \
    aur-server-client.c \
    aur-server-client.h

libaurena_server_la_CPPFLAGS = -I$(top_srcdir) $(AUR_COMMON_CFLAGS) $(GST_CFLAGS) $(GST_RTSP_CFLAGS) $(EXTRA_CFLAGS)
libaurena_server_la_LIBADD  =
