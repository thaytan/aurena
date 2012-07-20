SUBDIRS=daemon client

bin_PROGRAMS = sonarea simple-client
sonarea_CPPFLAGS = $(SNRA_COMMON_CFLAGS) $(GST_RTSP_CFLAGS) $(GST_CFLAGS) $(EXTRA_CFLAGS)
sonarea_LDADD = daemon/libsnra_daemon.la $(SNRA_COMMON_LIBS) $(GST_RTSP_LIBS) $(GST_LIBS)
sonarea_SOURCES = sonarea.c \
        snra-json.c snra-json.h \
        snra-types.h

simple_client_SOURCES = simple-client.c \
        snra-json.c snra-json.h \
        snra-types.h
simple_client_CPPFLAGS = $(SNRA_COMMON_CFLAGS) $(GST_CFLAGS) $(EXTRA_CFLAGS)
simple_client_LDADD = client/libsnra_client.la $(SNRA_COMMON_LIBS) $(GST_LIBS)
