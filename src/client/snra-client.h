#ifndef __SNRA_CLIENT_H__
#define __SNRA_CLIENT_H__

#include <gst/gst.h>
#include <gst/net/gstnet.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>

#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-glib/glib-watch.h>

#include <src/snra-types.h>

G_BEGIN_DECLS

#define SNRA_TYPE_CLIENT (snra_client_get_type ())

typedef struct _SnraClientClass SnraClientClass;

struct _SnraClient
{
  GObject parent;

  GstState state;
  gboolean enabled;
  gboolean paused;

  GstClock *net_clock;
  gchar *server_host;
  gint server_port;

  SoupSession *soup;
  JsonParser *json;

  GstElement *player;

  guint timeout;

  gboolean connecting;
  gboolean was_connected;
  gchar *connected_server;
  gint connected_port;

  AvahiGLibPoll *glib_poll;
  AvahiClient *avahi_client;
  AvahiServiceBrowser *avahi_sb;
};

struct _SnraClientClass
{
  GObjectClass parent;
};

GType snra_client_get_type(void);
SnraClient *snra_client_new(const gchar *server);

G_END_DECLS
#endif
