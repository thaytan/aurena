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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <libsoup/soup-server.h>
#include <libsoup/soup-message.h>
#include <libsoup/soup-socket.h>
#include <libsoup/soup-address.h>

#include <json-glib/json-glib.h>

#include "snra-server.h"
#include "snra-http-resource.h"

G_DEFINE_TYPE (SnraServer, snra_server, G_TYPE_OBJECT);

enum
{
  PROP_0,
  PROP_PORT,
  PROP_RTSP_PORT,
  PROP_CLOCK,
  PROP_LAST
};

typedef struct _SnraClientConnection SnraClientConnection;

struct _SnraClientConnection
{
  SoupMessage *msg;
};

static GParamSpec *obj_properties[PROP_LAST] = { NULL, };

static void snra_server_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void snra_server_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void snra_server_finalize(GObject *object);
static void snra_server_dispose(GObject *object);

static void
server_send_json_to_client (SnraServer *server, SnraClientConnection *client, JsonBuilder *builder)
{
  JsonGenerator *gen;
  JsonNode * root;
  gchar *body;

  gen = json_generator_new ();
  root = json_builder_get_root (builder);

  json_generator_set_root (gen, root);
  body = json_generator_to_data (gen, NULL);

  json_node_free (root);
  g_object_unref (gen);
  g_object_unref (builder);

  soup_message_body_append (client->msg->response_body,/* "application/json", */ SOUP_MEMORY_TAKE, body, strlen(body));
  soup_server_unpause_message (server->soup, client->msg);
}

static void
server_send_enrol_msg (SnraServer *server, SnraClientConnection *client)
{
  JsonBuilder *builder = json_builder_new ();
  int clock_port;
  GstClock *clock;
  GstClockTime cur_time;

  g_object_get (server->net_clock, "clock", &clock, NULL);
  cur_time = gst_clock_get_time (clock);
  gst_object_unref (clock);

  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, "msg-type");
  json_builder_add_string_value (builder, "enrol");

  g_object_get (server->net_clock, "port", &clock_port, NULL);
  json_builder_set_member_name (builder, "clock-port");
  json_builder_add_int_value (builder, clock_port);

  json_builder_set_member_name (builder, "current-time");
  json_builder_add_int_value (builder, (gint64)(cur_time));

  json_builder_end_object (builder);

  server_send_json_to_client (server, client, builder);
}

static void
server_send_play_media_msg (SnraServer *server, SnraClientConnection *client)
{
  JsonBuilder *builder = json_builder_new ();
  JsonGenerator *gen;
  JsonNode * root;
  gchar *body;
  int clock_port;
  GstClock *clock;
  GstClockTime cur_time;

  g_object_get (server->net_clock, "clock", &clock, NULL);
  cur_time = gst_clock_get_time (clock);
  gst_object_unref (clock);

  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, "msg-type");
  json_builder_add_string_value (builder, "play-media");

#if 1
  /* Serve via HTTP */
  json_builder_set_member_name (builder, "resource-protocol");
  json_builder_add_string_value (builder, "http");
  json_builder_set_member_name (builder, "resource-port");
  json_builder_add_int_value (builder, server->port);
#else
  /* Serve via RTSP */
  json_builder_set_member_name (builder, "resource-protocol");
  json_builder_add_string_value (builder, "rtsp");
  json_builder_set_member_name (builder, "resource-port");
  json_builder_add_int_value (builder, server->rtsp_port);
#endif

  json_builder_set_member_name (builder, "resource-path");
  json_builder_add_string_value (builder, "/resource/1");

  if (server->base_time == -1) {
    // configure a base time 0.5 seconds in the future
    server->base_time = cur_time + (GST_SECOND / 2);
    g_print ("Base time now %" G_GUINT64_FORMAT "\n", server->base_time);
  }

  json_builder_set_member_name (builder, "base-time");
  json_builder_add_int_value (builder, (gint64)(server->base_time));

  json_builder_end_object (builder);

  server_send_json_to_client (server, client, builder);
}

static void
server_client_cb (SoupServer *soup, SoupMessage *msg,
  const char *path, GHashTable *query,
  SoupClientContext *client, SnraServer *server)
{
  SnraClientConnection *client_conn = g_new0(SnraClientConnection, 1);

  client_conn->msg = msg;

  g_print("Got a hit on %s\n", path);
  soup_message_headers_set_encoding (msg->response_headers, SOUP_ENCODING_CHUNKED);
  soup_message_set_status (msg, SOUP_STATUS_OK);

  server_send_enrol_msg(server, client_conn);
  server_send_play_media_msg (server, client_conn);
}

static SnraHttpResource *
snra_server_get_resource(SnraServer *server, const char *resource_path)
{
  SnraHttpResource *resource = NULL;

  resource = g_hash_table_lookup (server->resources, resource_path); 
  if (resource == NULL) {
    resource = g_object_new (SNRA_TYPE_HTTP_RESOURCE, "source-path", server->test_path, NULL);
    g_hash_table_insert (server->resources, g_strdup(resource_path), resource);
    g_print ("Created resource %s\n", resource_path);
  }
  return resource;
}

static void
dump_header(const char *name, const char *value, gpointer user_data)
{
  // g_print("%s: %s\n", name, value);
}

static void
server_resource_cb (SoupServer *soup, SoupMessage *msg, 
  const char *path, GHashTable *query,
  SoupClientContext *client, SnraServer *server)
{
  const char *prefix = "/resource/";
  const char *resource_path;
  SnraHttpResource *resource;

  if (!g_str_has_prefix (path, prefix))
    goto error;

  resource_path = path + strlen(prefix);
  g_print ("Hit on resource %s\n", resource_path);
  soup_message_headers_foreach (msg->request_headers, dump_header, NULL);

  resource = snra_server_get_resource(server, resource_path);
  if (resource) {
    snra_http_resource_new_transfer (resource, server, msg);
  }
  
  return;
error:
  soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
}

void
snra_server_set_base_time(SnraServer *server, GstClockTime base_time)
{
  server->base_time = base_time;
}

void
snra_server_set_clock (SnraServer *server, GstNetTimeProvider *net_clock)
{
  server->net_clock = net_clock;
}

static void
snra_server_init (SnraServer *server)
{
  SoupSocket *socket;

  server->base_time = GST_CLOCK_TIME_NONE;
  server->port = 5457;

  server->soup = soup_server_new(SOUP_SERVER_PORT, server->port, NULL);
  soup_server_add_handler (server->soup, "/client", (SoupServerCallback) server_client_cb, g_object_ref (server), g_object_unref);
  soup_server_add_handler (server->soup, "/resource", (SoupServerCallback) server_resource_cb, g_object_ref (server), g_object_unref);
  soup_server_run_async (server->soup);

  socket = soup_server_get_listener(server->soup);
  if (socket) {
    SoupAddress *addr;
    g_object_get (socket, SOUP_SOCKET_LOCAL_ADDRESS, &addr, NULL);
    g_print ("Now listening on %s:%u\n", soup_address_get_name(addr), soup_address_get_port(addr));
    g_object_unref (addr);
  }

  server->resources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

static void
snra_server_class_init (SnraServerClass *server_class)
{
  GObjectClass *gobject_class = (GObjectClass *)(server_class);

  gobject_class->dispose = snra_server_dispose;
  gobject_class->finalize = snra_server_finalize;
  gobject_class->set_property = snra_server_set_property;
  gobject_class->get_property = snra_server_get_property;

  obj_properties[PROP_PORT] =
    g_param_spec_int ("port", "port",
                         "port for Sonarea service",
                         1, 65535, 5457,
                         G_PARAM_READWRITE);
  obj_properties[PROP_RTSP_PORT] =
    g_param_spec_int ("rtsp-port", "RTSP port",
                         "port for RTSP service",
                         1, 65535, 5458,
                         G_PARAM_READWRITE);
  obj_properties[PROP_CLOCK] =
    g_param_spec_object ("clock", "clock",
                         "clock to synchronise playback",
                         GST_TYPE_NET_TIME_PROVIDER,
                         G_PARAM_READWRITE);
  g_object_class_install_properties (gobject_class, PROP_LAST, obj_properties);
}

static void
snra_server_finalize(GObject *object)
{
  SnraServer *server = (SnraServer *)(object);
  g_object_unref (server->soup);
  g_hash_table_remove_all (server->resources);

  if (server->net_clock)
    gst_object_unref (server->net_clock);

  G_OBJECT_CLASS (snra_server_parent_class)->finalize (object);
}

static void
snra_server_dispose(GObject *object)
{
  SnraServer *server = (SnraServer *)(object);
  soup_server_quit (server->soup);

  G_OBJECT_CLASS (snra_server_parent_class)->dispose (object);
}

static void
snra_server_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  SnraServer *server = (SnraServer *)(object);

  switch (prop_id) {
    case PROP_PORT:
      server->port = g_value_get_int (value);
      break;
    case PROP_RTSP_PORT:
      server->rtsp_port = g_value_get_int (value);
      break;
    case PROP_CLOCK:
      if (server->net_clock)
        gst_object_unref (server->net_clock);
      server->net_clock = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
snra_server_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  SnraServer *server = (SnraServer *)(object);

  switch (prop_id) {
    case PROP_PORT:
      g_value_set_int (value, server->port);
      break;
    case PROP_RTSP_PORT:
      g_value_set_int (value, server->rtsp_port);
      break;
    case PROP_CLOCK:
      g_value_set_object (value, server->net_clock);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
