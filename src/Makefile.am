SUBDIRS=common client plugins
bin_PROGRAMS = aurena-simple-client

if BUILD_AUR_SERVER
SUBDIRS += server
bin_PROGRAMS += aurena-server
endif

if BUILD_GTKAPPS
bin_PROGRAMS += aurena-simple-client-fullscreen aurena-simple-controller
endif

aurena_server_CPPFLAGS = -I$(top_srcdir) $(AUR_COMMON_CFLAGS) $(AUR_SERVER_CFLAGS) $(GST_RTSP_CFLAGS) $(GST_CFLAGS) $(EXTRA_CFLAGS)
aurena_server_LDADD = server/libaurena_server.la common/libaurena_common.la $(AUR_COMMON_LIBS) $(AUR_SERVER_LIBS) $(GST_RTSP_LIBS) $(GST_LIBS)
aurena_server_SOURCES = aurena-server.c

aurena_simple_client_SOURCES = simple-client.c
aurena_simple_client_CPPFLAGS = -I$(top_srcdir) $(AUR_COMMON_CFLAGS) $(GST_CFLAGS) $(EXTRA_CFLAGS)
aurena_simple_client_LDADD = client/libaurena_client.la common/libaurena_common.la $(AUR_COMMON_LIBS) $(GST_LIBS)

aurena_simple_client_fullscreen_SOURCES = simple-client-fullscreen.c
aurena_simple_client_fullscreen_CPPFLAGS = $(aurena_simple_client_CPPFLAGS) \
        $(GTKAPPS_CFLAGS)
aurena_simple_client_fullscreen_LDADD = $(aurena_simple_client_LDADD) $(GTKAPPS_LIBS)

aurena_simple_controller_SOURCES = simple-controller.c
aurena_simple_controller_CPPFLAGS = $(aurena_simple_client_CPPFLAGS) \
        $(GTKAPPS_CFLAGS)
aurena_simple_controller_LDADD = $(aurena_simple_client_LDADD) $(GTKAPPS_LIBS)

EXTRA_DIST = gnome-client.py
