SUBDIRS=daemon client

bin_PROGRAMS = sonarea
sonarea_CPPFLAGS = $(SNRA_COMMON_CFLAGS) $(GST_RTSP_CFLAGS) $(GST_CFLAGS) $(EXTRA_CFLAGS)
sonarea_LDADD = daemon/libsnra_daemon.la $(SNRA_COMMON_LIBS) $(GST_RTSP_LIBS) $(GST_LIBS)

sonarea_SOURCES = sonarea.c \
        snra-json.c snra-json.h \
        snra-types.h
