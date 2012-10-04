SUBDIRS=client
bin_PROGRAMS = simple-client

if BUILD_SNRA_SERVER
SUBDIRS += daemon
bin_PROGRAMS += aurena
endif

aurena_CPPFLAGS = -I$(top_srcdir) $(SNRA_COMMON_CFLAGS) $(SNRA_SERVER_CFLAGS) $(GST_RTSP_CFLAGS) $(GST_CFLAGS) $(EXTRA_CFLAGS)
aurena_LDADD = daemon/libsnra_daemon.la $(SNRA_COMMON_LIBS) $(SNRA_SERVER_LIBS) $(GST_RTSP_LIBS) $(GST_LIBS)
aurena_SOURCES = aurena.c \
        snra-json.c snra-json.h \
        snra-types.h

simple_client_SOURCES = simple-client.c \
        snra-json.c snra-json.h \
        snra-types.h
simple_client_CPPFLAGS = -I$(top_srcdir) $(SNRA_COMMON_CFLAGS) $(GST_CFLAGS) $(EXTRA_CFLAGS)
simple_client_LDADD = client/libsnra_client.la $(SNRA_COMMON_LIBS) $(GST_LIBS)

EXTRA_DIST = gnome-client.py
DIST_SUBDIRS=client daemon static
