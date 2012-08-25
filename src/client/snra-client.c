/* GStreamer
 * Copyright (C) 2012 Jan Schmidt <thaytan@noraisin.net>
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

/*
 * Aurena Client is the central object which:
 *   creates the network clock
 *   Establishes libsoup session
 *   Creates RTSP sessions as needed
 *   Distributes the network clock and base time to clients
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <glib.h>

#if !GLIB_CHECK_VERSION(2,22,0)
/* GResolver not available */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

#include <src/snra-json.h>

#include "snra-client.h"

#define DISABLED_STATE GST_STATE_PAUSED

G_DEFINE_TYPE (SnraClient, snra_client, G_TYPE_OBJECT);

enum
{
  PROP_0,
  PROP_SERVER_HOST,
  PROP_LAST
};

static void snra_client_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void snra_client_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void snra_client_finalize (GObject * object);
static void snra_client_dispose (GObject * object);

static void search_for_server (SnraClient * client);
static void connect_to_server (SnraClient * client, const gchar * server,
    int port);
static void construct_player (SnraClient * client);

static gboolean
try_reconnect (SnraClient * client)
{
  client->timeout = 0;

  if (client->server_host)
    connect_to_server (client, client->server_host, client->server_port);
  else
    search_for_server (client);

  return FALSE;
}

static void
handle_connection_closed_cb (G_GNUC_UNUSED SoupSession * session,
    SoupMessage * msg, SnraClient * client)
{
  client->connecting = FALSE;

  if (msg->status_code == SOUP_STATUS_CANCELLED)
    return;

  if (client->was_connected) {
    g_print ("Disconnected from server. Reason %s status %d\n",
        msg->reason_phrase, msg->status_code);
  }
  client->was_connected = FALSE;

  if (client->player)
    gst_element_set_state (client->player, GST_STATE_READY);
  if (client->timeout == 0) {
    client->timeout =
        g_timeout_add_seconds (1, (GSourceFunc) try_reconnect, client);
  }
}

static void
handle_enrol_message (SnraClient * client, GstStructure * s)
{
  int clock_port;
  gint64 tmp;
  GstClockTime cur_time;
  gchar *server_ip_str = NULL;
  gdouble new_vol;

  if (!snra_json_structure_get_int (s, "clock-port", &clock_port))
    return;                     /* Invalid message */

  if (!snra_json_structure_get_int64 (s, "current-time", &tmp))
    return;                     /* Invalid message */
  cur_time = (GstClockTime) (tmp);

  if (snra_json_structure_get_double (s, "volume-level", &new_vol)) {
    if (client->player == NULL)
      construct_player (client);

    if (client->player) {
      //g_print ("New volume %g\n", new_vol);
      g_object_set (G_OBJECT (client->player), "volume", new_vol,
          "mute", (gboolean) (new_vol == 0.0), NULL);
    }
  }

  snra_json_structure_get_boolean (s, "enabled", &client->enabled);
  snra_json_structure_get_boolean (s, "paused", &client->paused);

#if GLIB_CHECK_VERSION(2,22,0)
  {
    GResolver *resolver = g_resolver_get_default ();
    GList *names;

    if (resolver == NULL)
      return;

    names =
        g_resolver_lookup_by_name (resolver, client->connected_server, NULL,
        NULL);
    if (names) {
      server_ip_str = g_inet_address_to_string ((GInetAddress *) (names->data));
      g_resolver_free_addresses (names);
    }
    g_object_unref (resolver);
  }
#else
  {
    struct addrinfo *names = NULL;
    if (getaddrinfo (client->connected_server, NULL, NULL, &names))
      return;
    if (names) {
      char hbuf[NI_MAXHOST];
      if (getnameinfo (names->ai_addr, names->ai_addrlen,
              hbuf, sizeof (hbuf), NULL, 0, NI_NUMERICHOST) == 0) {
        server_ip_str = g_strdup (hbuf);
      }
      freeaddrinfo (names);
    }
  }
#endif
  if (server_ip_str) {
    g_print ("Creating net clock at %s:%d time %" GST_TIME_FORMAT "\n",
        server_ip_str, clock_port, GST_TIME_ARGS (cur_time));
    if (client->net_clock)
      gst_object_unref (client->net_clock);
    client->net_clock = gst_net_client_clock_new ("net_clock", server_ip_str,
        clock_port, cur_time);
    g_free (server_ip_str);
  }
}

static void
on_eos_msg (G_GNUC_UNUSED GstBus * bus, G_GNUC_UNUSED GstMessage * msg,
    SnraClient * client)
{
  SoupMessage *soup_msg;
  /* FIXME: Next song should all be handled server side */
  char *url = g_strdup_printf ("http://%s:%u/control/next",
      client->connected_server, client->connected_port);

  g_print ("Got EOS message\n");

  soup_msg = soup_message_new ("GET", url);
  soup_session_send_message (client->soup, soup_msg);
  g_free (url);
}

static void
on_error_msg (G_GNUC_UNUSED GstBus * bus, GstMessage * msg,
    G_GNUC_UNUSED SnraClient * client)
{
  GError *err;
  gchar *dbg_info = NULL;

  gst_message_parse_error (msg, &err, &dbg_info);
  g_printerr ("ERROR from element %s: %s\n",
      GST_OBJECT_NAME (msg->src), err->message);
  g_printerr ("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");
  g_error_free (err);
  g_free (dbg_info);
}

static void
construct_player (SnraClient * client)
{
  GstBus *bus;

  if (GST_CHECK_VERSION (0, 11, 1))
    client->player = gst_element_factory_make ("playbin", NULL);
  else
    client->player = gst_element_factory_make ("playbin2", NULL);

  if (client->player == NULL) {
    g_warning ("Failed to construct playbin");
    return;
  }
  bus = gst_element_get_bus (GST_ELEMENT (client->player));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::eos", (GCallback) (on_eos_msg), client);
  g_signal_connect (bus, "message::error", (GCallback) (on_error_msg), client);
  gst_object_unref (bus);
}

static void
handle_set_media_message (SnraClient * client, GstStructure * s)
{
  const gchar *protocol, *path;
  int port;
  GstClockTime base_time;
  gint64 tmp;
  gchar *uri;
  gboolean paused;

  protocol = gst_structure_get_string (s, "resource-protocol");
  path = gst_structure_get_string (s, "resource-path");

  if (protocol == NULL || path == NULL)
    return;                     /* Invalid message */

  if (!snra_json_structure_get_int (s, "resource-port", &port))
    return;

  if (!snra_json_structure_get_int64 (s, "base-time", &tmp))
    return;                     /* Invalid message */

  if (!snra_json_structure_get_boolean (s, "paused", &paused))
    return;

  base_time = (GstClockTime) (tmp);

  if (client->player == NULL) {
    construct_player (client);
    if (client->player == NULL)
      return;
  } else {
    gst_element_set_state (client->player, GST_STATE_NULL);
  }

  uri =
      g_strdup_printf ("%s://%s:%d%s", protocol, client->connected_server, port,
      path);
  g_print ("Playing URI %s base_time %" GST_TIME_FORMAT "\n", uri,
      GST_TIME_ARGS (base_time));
  g_object_set (client->player, "uri", uri, NULL);
  g_free (uri);

  gst_element_set_start_time (client->player, GST_CLOCK_TIME_NONE);
  gst_element_set_base_time (client->player, base_time);
  gst_pipeline_use_clock (GST_PIPELINE (client->player), client->net_clock);

  if (client->enabled) {
    if (paused)
      client->state = GST_STATE_PAUSED;
    else
      client->state = GST_STATE_PLAYING;
  } else {
    client->state = DISABLED_STATE;
  }

  gst_element_set_state (client->player, client->state);
}

static void
handle_play_message (SnraClient * client, GstStructure * s)
{
  GstClockTime base_time;
  gint64 tmp;

  if (!snra_json_structure_get_int64 (s, "base-time", &tmp))
    return;                     /* Invalid message */
  base_time = (GstClockTime) (tmp);

  client->paused = FALSE;

  if (client->player) {
    GstClockTime stream_time =
        gst_clock_get_time (client->net_clock) - base_time;
    g_print ("Playing base_time %" GST_TIME_FORMAT " (offset %" GST_TIME_FORMAT
        ")\n", GST_TIME_ARGS (base_time), GST_TIME_ARGS (stream_time));
    gst_element_set_base_time (GST_ELEMENT (client->player), base_time);
    if (client->enabled == FALSE)
      client->state = DISABLED_STATE;
    else
      client->state = GST_STATE_PLAYING;
    gst_element_set_state (GST_ELEMENT (client->player), client->state);
  }
}

static void
handle_set_volume_message (SnraClient * client, GstStructure * s)
{
  gdouble new_vol;

  if (!snra_json_structure_get_double (s, "level", &new_vol))
    return;

  if (client->player == NULL)
    construct_player (client);

  if (client->player) {
    // g_print ("New volume %g\n", new_vol);
    g_object_set (G_OBJECT (client->player), "volume", new_vol,
        "mute", (gboolean) (new_vol == 0.0), NULL);
  }
}

static void
handle_set_client_message (SnraClient * client, GstStructure * s)
{
  if (!snra_json_structure_get_boolean (s, "enabled", &client->enabled))
    return;

  if (client->enabled == FALSE)
    client->state = DISABLED_STATE;
  else if (client->paused)
    client->state = GST_STATE_PAUSED;
  else
    client->state = GST_STATE_PLAYING;

  if (client->player)
    gst_element_set_state (GST_ELEMENT (client->player), client->state);
}

static void
handle_received_chunk (G_GNUC_UNUSED SoupMessage * msg, SoupBuffer * chunk,
    SnraClient * client)
{
  if (client->was_connected == FALSE) {
    g_print ("Successfully connected to server %s:%d\n",
        client->connected_server, client->connected_port);

    client->was_connected = TRUE;
  }
  /* Successful server connection, stop avahi discovery */
  if (client->avahi_client) {
    avahi_client_free (client->avahi_client);
    client->avahi_sb = NULL;
    client->avahi_client = NULL;
  }

  if (client->json == NULL)
    client->json = json_parser_new ();
#if 0
  {
    gchar *tmp = g_strndup (chunk->data, chunk->length);
    g_print ("%s\n", tmp);
    g_free (tmp);
  }
#endif
  if (json_parser_load_from_data (client->json, chunk->data, chunk->length,
          NULL)) {
    JsonNode *root = json_parser_get_root (client->json);
    GstStructure *s = snra_json_to_gst_structure (root);
    const char *msg_type;

    if (s == NULL)
      return;                   /* Invalid chunk */

    msg_type = gst_structure_get_string (s, "msg-type");
    if (msg_type == NULL || g_str_equal (msg_type, "ping")) {
      gst_structure_free (s);
      return;
    }

    if (g_str_equal (msg_type, "enrol"))
      handle_enrol_message (client, s);
    else if (g_str_equal (msg_type, "set-media"))
      handle_set_media_message (client, s);
    else if (g_str_equal (msg_type, "play"))
      handle_play_message (client, s);
    else if (g_str_equal (msg_type, "pause")) {
      client->paused = TRUE;
      if (client->enabled == FALSE)
        client->state = DISABLED_STATE;
      else
        client->state = GST_STATE_PAUSED;
      if (client->player)
        gst_element_set_state (GST_ELEMENT (client->player), client->state);
    } else if (g_str_equal (msg_type, "volume")) {
      handle_set_volume_message (client, s);
    } else if (g_str_equal (msg_type, "client-setting")) {
      handle_set_client_message (client, s);
    } else {
      g_print ("Unhandled event of type %s\n", msg_type);
    }
  }
}

static void
connect_to_server (SnraClient * client, const gchar * server, int port)
{
  SoupMessage *msg;
  char *url = g_strdup_printf ("http://%s:%u/client/player_events",
      server, port);
  if (client->connecting == FALSE) {
    g_print ("Attemping to connect to server %s:%d\n", server, port);
    client->connecting = TRUE;
  }
  g_free (client->connected_server);
  client->connected_server = g_strdup (server);
  client->connected_port = port;

  msg = soup_message_new ("GET", url);
  soup_message_body_set_accumulate (msg->response_body, FALSE);
  g_signal_connect (msg, "got-chunk", (GCallback) handle_received_chunk,
      client);
  soup_session_queue_message (client->soup, msg,
      (SoupSessionCallback) handle_connection_closed_cb, client);
  g_free (url);
}

static void
snra_client_init (SnraClient * client)
{
  client->soup = soup_session_async_new ();
  client->server_port = 5457;
  client->state = GST_STATE_NULL;
}

static void
snra_client_constructed (GObject * object)
{
  SnraClient *client = (SnraClient *) (object);

  if (G_OBJECT_CLASS (snra_client_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (snra_client_parent_class)->constructed (object);

  try_reconnect (client);
}

static void
snra_client_class_init (SnraClientClass * client_class)
{
  GObjectClass *gobject_class = (GObjectClass *) (client_class);

  gobject_class->constructed = snra_client_constructed;
  gobject_class->dispose = snra_client_dispose;
  gobject_class->finalize = snra_client_finalize;

  gobject_class->set_property = snra_client_set_property;
  gobject_class->get_property = snra_client_get_property;

  g_object_class_install_property (gobject_class, PROP_SERVER_HOST,
      g_param_spec_string ("server-host", "Aurena Server",
          "Aurena Server hostname or IP", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
snra_client_finalize (GObject * object)
{
  SnraClient *client = (SnraClient *) (object);

  if (client->avahi_sb)
    avahi_service_browser_free (client->avahi_sb);
  if (client->avahi_client)
    avahi_client_free (client->avahi_client);
  if (client->glib_poll)
    avahi_glib_poll_free (client->glib_poll);

  if (client->net_clock)
    gst_object_unref (client->net_clock);
  if (client->soup)
    g_object_unref (client->soup);
  if (client->json)
    g_object_unref (client->json);
  if (client->player) {
    GstBus *bus = gst_element_get_bus (client->player);
    gst_bus_remove_signal_watch (bus);
    gst_object_unref (bus);
    gst_object_unref (client->player);
  }

  g_free (client->server_host);
  g_free (client->connected_server);

  G_OBJECT_CLASS (snra_client_parent_class)->finalize (object);
}

static void
snra_client_dispose (GObject * object)
{
  SnraClient *client = (SnraClient *) (object);

  if (client->soup)
    soup_session_abort (client->soup);
  if (client->player)
    gst_element_set_state (client->player, GST_STATE_NULL);

  G_OBJECT_CLASS (snra_client_parent_class)->dispose (object);
}

static void
split_server_host (SnraClient * client)
{
  /* See if the client->server_host string has a : and split into
   * server:port if so */
  gchar *sep = g_strrstr (client->server_host, ":");

  if (sep) {
    gchar *server = g_strndup (client->server_host, sep - client->server_host);

    client->server_port = atoi (sep + 1);
    g_free (client->server_host);
    client->server_host = server;
  }
}

static void
snra_client_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  SnraClient *client = (SnraClient *) (object);

  switch (prop_id) {
    case PROP_SERVER_HOST:
      if (client->server_host)
        g_free (client->server_host);
      client->server_host = g_value_dup_string (value);
      if (client->server_host)
        split_server_host (client);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
snra_client_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  SnraClient *client = (SnraClient *) (object);

  switch (prop_id) {
    case PROP_SERVER_HOST:{
      gchar *tmp =
          g_strdup_printf ("%s:%u", client->server_host, client->server_port);
      g_value_take_string (value, tmp);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

SnraClient *
snra_client_new (const char *server_host)
{
  SnraClient *client = g_object_new (SNRA_TYPE_CLIENT,
      "server-host", server_host, NULL);
  return client;
}

static void
avahi_resolve_callback (AvahiServiceResolver * r,
    AVAHI_GCC_UNUSED AvahiIfIndex interface,
    AVAHI_GCC_UNUSED AvahiProtocol protocol, AvahiResolverEvent event,
    AVAHI_GCC_UNUSED const char *name, AVAHI_GCC_UNUSED const char *type,
    AVAHI_GCC_UNUSED const char *domain, const char *host_name,
    AVAHI_GCC_UNUSED const AvahiAddress * address, uint16_t port,
    AVAHI_GCC_UNUSED AvahiStringList * txt,
    AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
    AVAHI_GCC_UNUSED void *userdata)
{
  SnraClient *client = userdata;

  switch (event) {
    case AVAHI_RESOLVER_FAILURE:
      break;

    case AVAHI_RESOLVER_FOUND:{
      if (!client->connecting) {
        /* FIXME: Build a list of servers and try each one in turn? */
        connect_to_server (client, host_name, port);
      }
    }
  }

  avahi_service_resolver_free (r);
}

static void
browse_callback (AVAHI_GCC_UNUSED AvahiServiceBrowser * b,
    AvahiIfIndex interface, AvahiProtocol protocol,
    AvahiBrowserEvent event,
    const char *name, const char *type, const char *domain,
    AVAHI_GCC_UNUSED AvahiLookupResultFlags flags, void *userdata)
{
  SnraClient *client = userdata;

  switch (event) {
    case AVAHI_BROWSER_FAILURE:
      /* Respawn browser on a timer? */
      avahi_service_browser_free (client->avahi_sb);
      client->timeout = g_timeout_add_seconds (1,
          (GSourceFunc) try_reconnect, client);
      return;

    case AVAHI_BROWSER_NEW:{
      avahi_service_resolver_new (client->avahi_client, interface,
          protocol, name, type, domain, AVAHI_PROTO_UNSPEC, 0,
          avahi_resolve_callback, client);
      break;
    }
    case AVAHI_BROWSER_REMOVE:
    case AVAHI_BROWSER_ALL_FOR_NOW:
    case AVAHI_BROWSER_CACHE_EXHAUSTED:
      break;
  }
}

static void
snra_avahi_client_callback (AvahiClient * s, AvahiClientState state,
    SnraClient * client)
{
  switch (state) {
    case AVAHI_CLIENT_S_RUNNING:{
      if (client->avahi_sb == NULL) {
        g_print ("Looking for new broadcast servers\n");
        client->avahi_sb = avahi_service_browser_new (s, AVAHI_IF_UNSPEC,
            AVAHI_PROTO_UNSPEC, "_aurena._tcp", NULL, 0, browse_callback,
            client);
        if (client->avahi_sb == NULL) {
          fprintf (stderr, "Failed to create service browser: %s\n",
              avahi_strerror (avahi_client_errno (client->avahi_client)));
        }
      }
      break;
    }
    default:
      break;
  }
}

static void
search_for_server (SnraClient * client)
{
  const AvahiPoll *poll_api;
  int error;

  if (client->glib_poll == NULL) {
    client->glib_poll = avahi_glib_poll_new (NULL, G_PRIORITY_DEFAULT);
    if (client->glib_poll == NULL)
      return;
  }

  poll_api = avahi_glib_poll_get (client->glib_poll);

  if (client->avahi_client == NULL) {
    client->avahi_client =
        avahi_client_new (poll_api, AVAHI_CLIENT_NO_FAIL,
        (AvahiClientCallback) snra_avahi_client_callback, client, &error);
    if (client->avahi_client == NULL) {
      fprintf (stderr, "Failed to connect to Avahi: %s",
          avahi_strerror (error));
      return;
    }
  }

}
