SUBDIRS=client
bin_PROGRAMS = aurena-simple-client

if BUILD_AUR_SERVER
SUBDIRS += server
bin_PROGRAMS += aurena-server
endif

if BUILD_GTKAPPS
bin_PROGRAMS += aurena-simple-client-fullscreen aurena-simple-controller
endif

COMMON_HDRS = \
  common/aur-types.h \
  common/aur-component.h \
  common/aur-component-link.h \
  common/aur-config.h \
  common/aur-event.h \
  common/aur-json.h \
  common/aur-websocket-parser.h

COMMON_SOURCES = \
  common/aur-component.c \
  common/aur-component-link.c \
  common/aur-config.c \
  common/aur-event.c \
  common/aur-json.c \
  common/aur-websocket-parser.c

aurena_server_CPPFLAGS = -I$(top_srcdir) $(AUR_COMMON_CFLAGS) $(AUR_SERVER_CFLAGS) $(GST_RTSP_CFLAGS) $(GST_CFLAGS) $(EXTRA_CFLAGS)
aurena_server_LDADD = server/libaurena_server.la $(AUR_COMMON_LIBS) $(AUR_SERVER_LIBS) $(GST_RTSP_LIBS) $(GST_LIBS)
aurena_server_SOURCES = aurena-server.c $(COMMON_SOURCES)

aurena_simple_client_SOURCES = simple-client.c $(COMMON_SOURCES)
aurena_simple_client_CPPFLAGS = -I$(top_srcdir) $(AUR_COMMON_CFLAGS) $(GST_CFLAGS) $(EXTRA_CFLAGS)
aurena_simple_client_LDADD = client/libaurena_client.la $(AUR_COMMON_LIBS) $(GST_LIBS)

aurena_simple_client_fullscreen_SOURCES = simple-client-fullscreen.c $(COMMON_SOURCES)
aurena_simple_client_fullscreen_CPPFLAGS = $(aurena_simple_client_CPPFLAGS) \
        $(GTKAPPS_CFLAGS)
aurena_simple_client_fullscreen_LDADD = $(aurena_simple_client_LDADD) $(GTKAPPS_LIBS)


aurena_simple_controller_SOURCES = simple-controller.c $(COMMON_SOURCES)
aurena_simple_controller_CPPFLAGS = $(aurena_simple_client_CPPFLAGS) \
        $(GTKAPPS_CFLAGS)
aurena_simple_controller_LDADD = $(aurena_simple_client_LDADD) $(GTKAPPS_LIBS)

noinst_HEADERS = $(COMMON_HDRS)
EXTRA_DIST = gnome-client.py
DIST_SUBDIRS = client server
