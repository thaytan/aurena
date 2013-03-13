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

#include "src/snra-types.h"

G_BEGIN_DECLS

#define SNRA_TYPE_CLIENT (snra_client_get_type ())

typedef struct _SnraClientClass SnraClientClass;

typedef struct
{
  guint id;
  gchar * host;
  gdouble volume;
  gboolean enabled;
} SnraPlayerInfo;

typedef enum
{
    SNRA_CLIENT_NONE = 0,
    SNRA_CLIENT_PLAYER = (1 << 0),
    SNRA_CLIENT_CONTROLLER = (1 << 1),
} SnraClientFlags;

struct _SnraClient
{
  GObject parent;

  SnraClientFlags flags;
  gdouble volume;
  GArray *player_info;

  gboolean enabled;
  gboolean paused;
  GstClockTime base_time;
  GstClockTime position;
  gchar *uri;

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
SnraClient *snra_client_new(const gchar *server, SnraClientFlags flags);

gboolean snra_client_is_connected (SnraClient * client);
gboolean snra_client_is_enabled (SnraClient * client);
gboolean snra_client_is_playing (SnraClient * client);
void snra_client_set_media (SnraClient * client, const gchar * id);
void snra_client_next (SnraClient * client, guint id);
void snra_client_play (SnraClient * client);
void snra_client_pause (SnraClient * client);
void snra_client_seek (SnraClient * client, GstClockTime position);
void snra_client_get_volume (SnraClient * client, gdouble volume);
void snra_client_set_volume (SnraClient * client, gdouble volume);
const GArray *snra_client_get_player_info (SnraClient * client);
gboolean snra_client_get_player_enabled (SnraClient * client, guint id);
void snra_client_set_player_enabled (SnraClient * client, guint id, gboolean enabled);
void snra_client_set_player_volume (SnraClient * client, guint id, gdouble volume);

G_END_DECLS
#endif
