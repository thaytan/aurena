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
  connect_to_server (client, client->server_host);
  return FALSE;
}

static void
handle_connection_closed_cb (SoupSession *session, SoupMessage *msg, SnraClient *client)
{
  g_print ("HTTP connection closed, status %d (%s)\n", msg->status_code, msg->reason_phrase);
  g_timeout_add_seconds (1, (GSourceFunc) try_reconnect, client);
}

static void
handle_received_chunk (SoupMessage *msg, SoupBuffer *chunk, SnraClient *client)
{
  if (client->json == NULL)
    client->json = json_parser_new();
  if (json_parser_load_from_data (client->json, chunk->data, chunk->length, NULL)) {
    JsonReader *reader = json_reader_new (json_parser_get_root (client->json));
    g_print ("Parsed a chunk of JSON\n");
    g_object_unref (reader);
  }
}

static void
connect_to_server (SnraClient *client, const gchar *server)
{
  SoupMessage *msg;
  char *url = g_strdup_printf ("http://%s:5457/client", server);

  g_print ("Requesting %s\n", url);
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
