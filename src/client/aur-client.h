/* GStreamer
 * Copyright (C) 2012-2014 Jan Schmidt <thaytan@noraisin.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifndef __AUR_CLIENT_H__
#define __AUR_CLIENT_H__

#include <gst/gst.h>
#include <gst/net/gstnet.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#if HAVE_AVAHI
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-glib/glib-watch.h>
#endif

#include <common/aur-component.h>
#include <client/aur-client-types.h>

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE(AurClient, aur_client, AUR, CLIENT, GObject);
#define AUR_TYPE_CLIENT (aur_client_get_type ())

typedef struct
{
  guint id;
  gchar * host;
  gdouble volume;
  gboolean enabled;
} AurPlayerInfo;

typedef enum
{
    AUR_CLIENT_NONE = 0,
    AUR_CLIENT_CONTROLLER = AUR_COMPONENT_ROLE_CONTROLLER,
    AUR_CLIENT_PLAYER = AUR_COMPONENT_ROLE_PLAYER,
    AUR_CLIENT_CAPTURE = AUR_COMPONENT_ROLE_CAPTURE
} AurClientRoles;

struct _AurClient
{
  GObject parent;

  guint id;
  gchar *client_name;

  AurClientRoles roles;
  gdouble volume;
  GArray *player_info;

  guint track_seqid;
  gboolean enabled;
  gboolean paused;
  GstClockTime base_time;
  GstClockTime position;
  gchar *uri;
  gchar *language;

  GstClock *net_clock;
  gchar *server_host;
  gint server_port;

  SoupSession *soup;
  SoupMessage *msg;
  JsonParser *json;

  GMainContext * context;

  GstElement *player;
  GstBus *bus;
  GSource *bus_source;

  GSource *recon_timeout;
  GSource *idle_timeout;

  gboolean connecting;
  gboolean was_connected;
  gboolean shutting_down;
  gchar *connected_server;
  gint connected_port;

#if HAVE_AVAHI
  AvahiGLibPoll *glib_poll;
  AvahiClient *avahi_client;
  AvahiServiceBrowser *avahi_sb;
#endif

  GstElement *record_pipe;
  gchar *record_dest;
};

struct _AurClientClass
{
  GObjectClass parent;
};

GType aur_client_get_type(void);
AurClient *aur_client_new(GMainContext * context, const gchar *server, AurClientRoles roles, const gchar *client_name);

gboolean aur_client_is_connected (AurClient * client);
gboolean aur_client_is_enabled (AurClient * client);
gboolean aur_client_is_playing (AurClient * client);
void aur_client_set_media (AurClient * client, const gchar * id);
void aur_client_next (AurClient * client, guint id);
void aur_client_eos (AurClient * client);
void aur_client_play (AurClient * client);
void aur_client_pause (AurClient * client);
void aur_client_seek (AurClient * client, GstClockTime position);
void aur_client_get_volume (AurClient * client, gdouble volume);
void aur_client_set_volume (AurClient * client, gdouble volume);
const GArray *aur_client_get_player_info (AurClient * client);
gboolean aur_client_get_player_enabled (AurClient * client, guint id);
void aur_client_set_player_enabled (AurClient * client, guint id, gboolean enabled);
void aur_client_set_player_volume (AurClient * client, guint id, gdouble volume);
void aur_client_set_language (AurClient * client, const gchar *language_code);

G_END_DECLS
#endif
