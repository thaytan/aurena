noinst_LTLIBRARIES        = libaurena_server.la

libaurena_server_la_SOURCES = \
    aur-avahi.c \
    aur-http-resource.c \
    aur-manager.c \
    aur-media-db.c \
    aur-resource.c \
    aur-receiver.c \
    aur-rtsp-play-media.c \
    aur-server.c \
    aur-server-client.c

noinst_HEADERS = \
    aur-avahi.h \
    aur-http-resource.h \
    aur-manager.h \
    aur-media-db.h \
    aur-resource.h \
    aur-rtsp-play-media.h \
    aur-server.h \
    aur-server-client.h \
    aur-server-types.h

libaurena_server_la_CPPFLAGS = -I$(top_srcdir) $(AUR_COMMON_CFLAGS) $(GST_CFLAGS) $(GST_RTSP_CFLAGS) $(EXTRA_CFLAGS)
libaurena_server_la_LIBADD  =
