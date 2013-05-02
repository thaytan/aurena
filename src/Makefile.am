SUBDIRS=client
bin_PROGRAMS = aurena-simple-client

if BUILD_SNRA_SERVER
SUBDIRS += daemon
bin_PROGRAMS += aurena-server
endif

if BUILD_GTKAPPS
bin_PROGRAMS += aurena-simple-client-fullscreen aurena-simple-controller
endif

aurena_server_CPPFLAGS = -I$(top_srcdir) $(SNRA_COMMON_CFLAGS) $(SNRA_SERVER_CFLAGS) $(GST_RTSP_CFLAGS) $(GST_CFLAGS) $(EXTRA_CFLAGS)
aurena_server_LDADD = daemon/libsnra_daemon.la $(SNRA_COMMON_LIBS) $(SNRA_SERVER_LIBS) $(GST_RTSP_LIBS) $(GST_LIBS)
aurena_server_SOURCES = aurena.c \
        snra-json.c snra-json.h \
        snra-types.h

aurena_simple_client_SOURCES = simple-client.c \
        snra-json.c snra-json.h \
        snra-types.h
aurena_simple_client_CPPFLAGS = -I$(top_srcdir) $(SNRA_COMMON_CFLAGS) $(GST_CFLAGS) $(EXTRA_CFLAGS)
aurena_simple_client_LDADD = client/libsnra_client.la $(SNRA_COMMON_LIBS) $(GST_LIBS)


aurena_simple_client_fullscreen_SOURCES = simple-client-fullscreen.c \
        snra-json.c snra-json.h \
        snra-types.h
aurena_simple_client_fullscreen_CPPFLAGS = $(aurena_simple_client_CPPFLAGS) \
        $(GTKAPPS_CFLAGS)
aurena_simple_client_fullscreen_LDADD = $(aurena_simple_client_LDADD) $(GTKAPPS_LIBS)


aurena_simple_controller_SOURCES = simple-controller.c \
        snra-json.c snra-json.h \
        snra-types.h
aurena_simple_controller_CPPFLAGS = $(aurena_simple_client_CPPFLAGS) \
        $(GTKAPPS_CFLAGS)
aurena_simple_controller_LDADD = $(aurena_simple_client_LDADD) $(GTKAPPS_LIBS)


EXTRA_DIST = gnome-client.py
DIST_SUBDIRS = client daemon static
