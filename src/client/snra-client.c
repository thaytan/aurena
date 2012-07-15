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
 * Sonarea Client is the central object which:
 *   creates the network clock
 *   Establishes libsoup session
 *   Creates RTSP sessions as needed
 *   Distributes the network clock and base time to clients
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "snra-client.h"

G_DEFINE_TYPE (SnraClient, snra_client, G_TYPE_OBJECT);

enum
{
  PROP_0,
  PROP_SERVER_HOST,
  PROP_LAST
};

static GParamSpec *obj_properties[PROP_LAST] = { NULL, };

static void snra_client_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void snra_client_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void snra_client_finalize(GObject *object);
static void snra_client_dispose(GObject *object);

static void connect_to_server (SnraClient *client, const gchar *server);

static gboolean
try_reconnect (SnraClient *client)
{
  client->timeout = 0;

  connect_to_server (client, client->server_host);
  return FALSE;
}

static void
handle_connection_closed_cb (SoupSession *session, SoupMessage *msg, SnraClient *client)
{
  g_print ("HTTP connection closed, status %d (%s)\n", msg->status_code, msg->reason_phrase);
  if (client->player)
    gst_element_set_state (client->player, GST_STATE_READY);
  if (client->timeout == 0)
    client->timeout = g_timeout_add_seconds (1, (GSourceFunc) try_reconnect, client);
}

static void
handle_enrol_message (SnraClient *client, JsonReader *reader)
{
  int clock_port;
  GstClockTime cur_time;
  GResolver *resolver;
  GList *names;
 
  if (!json_reader_read_member (reader, "clock-port"))
    return; /* Invalid message */
  clock_port = json_reader_get_int_value (reader);
  json_reader_end_member (reader);

  if (!json_reader_read_member (reader, "current-time"))
    return; /* Invalid message */
  cur_time = (GstClockTime)(json_reader_get_int_value (reader));
  json_reader_end_member (reader);

  resolver = g_resolver_get_default ();
  if (resolver == NULL)
    return;

  names = g_resolver_lookup_by_name (resolver, client->server_host, NULL, NULL);
  if (names) {
    gchar *name = g_inet_address_to_string ((GInetAddress *)(names->data));

    if (client->net_clock)
      gst_object_unref (client->net_clock);
    g_print ("Creating net clock at %s:%d time %" GST_TIME_FORMAT "\n", name, clock_port, GST_TIME_ARGS (cur_time));
    client->net_clock = gst_net_client_clock_new ("net_clock", name, clock_port, cur_time);

    g_resolver_free_addresses (names);
  }

  g_object_unref (resolver);
}

static void
on_eos_msg (GstBus *bus, GstMessage *msg, SnraClient *client)
{
  SoupMessage *soup_msg;
  char *url = g_strdup_printf ("http://%s:5457/control/next", client->server_host);

  g_print ("Got EOS message\n");

  soup_msg = soup_message_new ("GET", url);
  soup_session_send_message (client->soup, soup_msg);
  g_free (url);
}

static void
on_error_msg (GstBus *bus, GstMessage *msg, SnraClient *client)
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
construct_player (SnraClient *client)
{
  GstBus *bus;

  client->player = gst_element_factory_make ("playbin", NULL);
  if (client->player == NULL) {
    g_warning ("Failed to construct playbin");
    return;
  }
  bus = gst_element_get_bus (GST_ELEMENT (client->player));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::eos", (GCallback)(on_eos_msg), client);
  g_signal_connect (bus, "message::error", (GCallback)(on_error_msg), client);
  gst_object_unref (bus);
}

static void
handle_play_media_message (SnraClient *client, JsonReader *reader)
{
  const gchar *protocol, *path;
  int port;
  GstClockTime base_time;
  gchar *uri;
  
  if (!json_reader_read_member (reader, "resource-protocol"))
    return; /* Invalid message */
  protocol = json_reader_get_string_value (reader);
  json_reader_end_member (reader);

  if (!json_reader_read_member (reader, "resource-port"))
    return; /* Invalid message */
  port = json_reader_get_int_value (reader);
  json_reader_end_member (reader);

  if (!json_reader_read_member (reader, "resource-path"))
    return; /* Invalid message */
  path = json_reader_get_string_value (reader);
  json_reader_end_member (reader);

  if (!json_reader_read_member (reader, "base-time"))
    return; /* Invalid message */
  base_time = json_reader_get_int_value (reader);
  json_reader_end_member (reader);

  if (client->player == NULL) {
    construct_player (client);
    if (client->player == NULL)
      return;
  }
  else {
    gst_element_set_state (client->player, GST_STATE_NULL);
  }

  uri = g_strdup_printf ("%s://%s:%d%s", protocol, client->server_host, port, path);
  g_print ("Playing URI %s base_time %" GST_TIME_FORMAT "\n", uri, GST_TIME_ARGS (base_time));
  g_object_set (client->player, "uri", uri, NULL);
  g_free (uri);

  gst_element_set_start_time (client->player, GST_CLOCK_TIME_NONE);
  gst_element_set_base_time (client->player, base_time);
  gst_pipeline_use_clock (GST_PIPELINE (client->player), client->net_clock);
  
  gst_element_set_state (client->player, GST_STATE_PLAYING);
}

static void
handle_play_message (SnraClient *client, JsonReader *reader)
{
  GstClockTime base_time;

  if (!json_reader_read_member (reader, "base-time"))
    return; /* Invalid message */
  base_time = json_reader_get_int_value (reader);
  json_reader_end_member (reader);

  if (client->player) {
    GstClockTime stream_time = gst_clock_get_time (client->net_clock) - base_time;
    g_print ("Playing base_time %" GST_TIME_FORMAT " (offset %" GST_TIME_FORMAT ")\n",
        GST_TIME_ARGS (base_time), GST_TIME_ARGS (stream_time));
    gst_element_set_base_time (GST_ELEMENT (client->player), base_time);
    gst_element_set_state (GST_ELEMENT (client->player), GST_STATE_PLAYING);
  }
}

static void
handle_set_volume_message (SnraClient *client, JsonReader *reader)
{
  gdouble new_vol;

  if (!json_reader_read_member (reader, "level"))
    return; /* Invalid message */
  new_vol = json_reader_get_double_value (reader);

  json_reader_end_member (reader);

  if (new_vol == 0) {
    if (!json_reader_read_member (reader, "level"))
      return; /* Invalid message */
    new_vol = (double)(json_reader_get_int_value (reader));
    json_reader_end_member (reader);
  }

  if (client->player) {
    g_print ("New volume %g\n", new_vol);
    g_object_set (G_OBJECT (client->player), "volume", new_vol,
        "mute", (gboolean)(new_vol == 0.0), NULL);
  }
}

static void
handle_received_chunk (SoupMessage *msg, SoupBuffer *chunk, SnraClient *client)
{
  if (client->json == NULL)
    client->json = json_parser_new();
#if 0
  {
    gchar *tmp = g_strndup (chunk->data, chunk->length);
    g_print ("%s\n", tmp);
    g_free (tmp);
  }
#endif
  if (json_parser_load_from_data (client->json, chunk->data, chunk->length, NULL)) {
    JsonReader *reader = json_reader_new (json_parser_get_root (client->json));
    if (json_reader_read_member (reader, "msg-type")) {
      const char *msg_type = json_reader_get_string_value (reader);
      json_reader_end_member (reader);
      g_print ("event of type %s\n", msg_type);
      if (g_str_equal (msg_type, "enrol"))
        handle_enrol_message (client, reader);
      else if (g_str_equal (msg_type, "play-media"))
        handle_play_media_message (client, reader);
      else if (g_str_equal (msg_type, "play"))
        handle_play_message (client, reader);
      else if (g_str_equal (msg_type, "pause")) {
        if (client->player)
            gst_element_set_state (GST_ELEMENT (client->player), GST_STATE_PAUSED);
      }
      else if (g_str_equal (msg_type, "volume"))
        handle_set_volume_message (client, reader);
    }
    g_object_unref (reader);
  }
}

static void
connect_to_server (SnraClient *client, const gchar *server)
{
  SoupMessage *msg;
  char *url = g_strdup_printf ("http://%s:5457/client", server);

  msg = soup_message_new ("GET", url);
  soup_message_body_set_accumulate (msg->response_body, FALSE);
  g_signal_connect (msg, "got-chunk", (GCallback) handle_received_chunk, client);
  soup_session_queue_message (client->soup, msg, (SoupSessionCallback) handle_connection_closed_cb, client);
  g_free (url);
}

static void
snra_client_init (SnraClient *client)
{
  client->soup = soup_session_async_new();
}

static void
snra_client_constructed (GObject *object)
{
  SnraClient *client = (SnraClient *)(object);

  G_OBJECT_CLASS (snra_client_parent_class)->constructed (G_OBJECT (client));

  if (client->server_host)
    connect_to_server (client, client->server_host);
}

static void
snra_client_class_init (SnraClientClass *client_class)
{
  GObjectClass *gobject_class = (GObjectClass *)(client_class);

  gobject_class->constructed = snra_client_constructed;
  gobject_class->set_property = snra_client_set_property;
  gobject_class->get_property = snra_client_get_property;

  obj_properties[PROP_SERVER_HOST] =
    g_param_spec_string ("server-host", "Sonarea Server", "Sonarea Server hostname or IP",
                         NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
  g_object_class_install_properties (gobject_class, PROP_LAST, obj_properties);
}

static void
snra_client_finalize(GObject *object)
{
  SnraClient *client = (SnraClient *)(object);

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

  G_OBJECT_CLASS (snra_client_parent_class)->finalize (object);
}

static void
snra_client_dispose(GObject *object)
{
  SnraClient *client = (SnraClient *)(object);

  if (client->soup)
    soup_session_abort (client->soup);

  G_OBJECT_CLASS (snra_client_parent_class)->dispose (object);
}

static void
snra_client_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  SnraClient *client = (SnraClient *)(object);

  switch (prop_id) {
    case PROP_SERVER_HOST:
      if (client->server_host)
        g_free (client->server_host);
      client->server_host = g_value_dup_string (value);
      g_print ("Host %s\n", client->server_host);
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
  SnraClient *client = (SnraClient *)(object);

  switch (prop_id) {
    case PROP_SERVER_HOST:
      g_value_set_string (value, client->server_host);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

SnraClient *
snra_client_new(const char *server_host)
{
  SnraClient *client = g_object_new (SNRA_TYPE_CLIENT, "server-host", server_host, NULL);

  return client;
}
