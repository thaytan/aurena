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
#include <stdio.h>
#include <libsoup/soup-server.h>
#include <libsoup/soup-message.h>
#include <libsoup/soup-socket.h>
#include <libsoup/soup-address.h>
#include <json-glib/json-glib.h>

#include <src/snra-json.h>

#include "snra-server.h"
#include "snra-resource.h"
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

struct _SnraClientConnection
{
  SoupMessage *msg;
};

static void snra_server_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void snra_server_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void snra_soup_message_set_redirect (SoupMessage *msg,
    guint status_code, const char *redirect_uri);
static void snra_server_finalize(GObject *object);
static void snra_server_dispose(GObject *object);
static void free_client_connection (gpointer data);

static void
server_send_msg_to_client (SnraServer *server, SnraClientConnection *client,
    GstStructure *msg)
{
  JsonGenerator *gen;
  JsonNode *root;
  gchar *body;
  gsize len;

  root = snra_json_from_gst_structure(msg);
  gst_structure_free(msg);

  gen = json_generator_new ();

  json_generator_set_root (gen, root);

  body = json_generator_to_data (gen, &len);

  g_object_unref (gen);
  json_node_free (root);

  if (client) {
    soup_message_body_append (client->msg->response_body,/* "application/json", */
        SOUP_MEMORY_TAKE, body, len);
    soup_server_unpause_message (server->soup, client->msg);
  }
  else {
    /* client == NULL - send to all clients */
    GList *cur;
    for (cur = server->clients; cur != NULL; cur = g_list_next (cur)) {
      client = (SnraClientConnection *)(cur->data);
      soup_message_body_append (client->msg->response_body,/* "application/json", */
          SOUP_MEMORY_COPY, body, len);
      soup_server_unpause_message (server->soup, client->msg);
    }
    g_free (body);
  }
}

void
server_send_enrol_msg (SnraServer *server, SnraClientConnection *client)
{
  int clock_port;
  GstClock *clock;
  GstClockTime cur_time;
  GstStructure *msg;

  g_object_get (server->net_clock, "clock", &clock, NULL);
  cur_time = gst_clock_get_time (clock);
  gst_object_unref (clock);

  g_object_get (server->net_clock, "port", &clock_port, NULL);

  msg = gst_structure_new ("json",
            "msg-type", G_TYPE_STRING, "enrol",
            "clock-port", G_TYPE_INT, clock_port,
            "current-time", G_TYPE_INT64, (gint64)(cur_time),
            "volume-level", G_TYPE_DOUBLE, server->current_volume,
            NULL);

  server_send_msg_to_client (server, client, msg);
}

static void
server_send_play_media_msg (SnraServer *server, SnraClientConnection *client,
    guint resource_id)
{
  GstClock *clock;
  GstClockTime cur_time;
  gchar *resource_path;
  GstStructure *msg;

  g_object_get (server->net_clock, "clock", &clock, NULL);
  cur_time = gst_clock_get_time (clock);
  gst_object_unref (clock);

  resource_path = g_strdup_printf ("/resource/%u", resource_id);

  if (server->base_time == GST_CLOCK_TIME_NONE) {
    // configure a base time 0.25 seconds in the future
    server->base_time = cur_time + (GST_SECOND / 4);
    server->stream_time = GST_CLOCK_TIME_NONE;
    g_print ("Base time now %" G_GUINT64_FORMAT "\n", server->base_time);
  }

  msg = gst_structure_new ("json",
            "msg-type", G_TYPE_STRING, "play-media",
#if 1
            "resource-protocol", G_TYPE_STRING, "http",
            "resource-port", G_TYPE_INT, server->port,
#else
            "resource-protocol", G_TYPE_STRING, "rtsp",
            "resource-port", G_TYPE_INT, server->rtsp_port,
#endif
            "resource-path", G_TYPE_STRING, resource_path,
            "base-time", G_TYPE_INT64, (gint64)(server->base_time),
            NULL);

  g_free (resource_path);

  server_send_msg_to_client (server, client, msg);
}

static gint
find_client_by_message (SnraClientConnection *client, SoupMessage *wanted)
{
  if (client->msg == wanted)
    return 0;
  return 1;
}

static void
server_client_disconnect (SoupMessage *message, SnraServer *server)
{
  GList *client = g_list_find_custom (server->clients, message, (GCompareFunc)(find_client_by_message));

  if (client) {
    free_client_connection ((SnraClientConnection *)(client->data));
    server->clients = g_list_delete_link (server->clients, client);
  }
}

static void
server_client_cb (G_GNUC_UNUSED SoupServer *soup, SoupMessage *msg,
  G_GNUC_UNUSED const char *path, G_GNUC_UNUSED GHashTable *query,
  G_GNUC_UNUSED SoupClientContext *client, SnraServer *server)
{
  SnraClientConnection *client_conn = g_new0(SnraClientConnection, 1);

  client_conn->msg = msg;

  soup_message_headers_set_encoding (msg->response_headers, SOUP_ENCODING_CHUNKED);
  soup_message_set_status (msg, SOUP_STATUS_OK);

  g_signal_connect (msg, "finished", G_CALLBACK (server_client_disconnect), server);

  server->clients = g_list_prepend (server->clients, client_conn);

  server_send_enrol_msg(server, client_conn);
  if (server->current_resource)
    server_send_play_media_msg (server, client_conn, server->current_resource);
}

static void
snra_soup_message_set_redirect (SoupMessage *msg, guint status_code,
    const char *redirect_uri)
{
        SoupURI *location;
        char *location_str;

        location = soup_uri_new_with_base (soup_message_get_uri (msg), redirect_uri);
        g_return_if_fail (location != NULL);

        soup_message_set_status (msg, status_code);
        location_str = soup_uri_to_string (location, FALSE);
        soup_message_headers_replace (msg->response_headers, "Location",
                                      location_str);
        g_free (location_str);
        soup_uri_free (location);
}

static void
server_fallback_cb (G_GNUC_UNUSED SoupServer *soup, SoupMessage *msg,
  G_GNUC_UNUSED const char *path, G_GNUC_UNUSED GHashTable *query,
  G_GNUC_UNUSED SoupClientContext *client, G_GNUC_UNUSED SnraServer *server)
{
  if (g_str_equal (path, "/")) {
    snra_soup_message_set_redirect (msg, SOUP_STATUS_MOVED_PERMANENTLY, "/ui/");
  }
  else {
    soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
  }
}

static SnraHttpResource *
snra_server_get_resource(SnraServer *server, guint resource_id)
{
  SnraHttpResource *resource = NULL;

  resource = g_hash_table_lookup (server->resources, GINT_TO_POINTER (resource_id));
  if (resource == NULL) {
    resource = server->get_resource (server, resource_id, server->get_resource_userdata);
    g_hash_table_insert (server->resources, GINT_TO_POINTER (resource_id), resource);
  }
  return resource;
}

static void
server_resource_cb (G_GNUC_UNUSED SoupServer *soup, SoupMessage *msg, 
  const char *path, G_GNUC_UNUSED GHashTable *query,
  G_GNUC_UNUSED SoupClientContext *client, SnraServer *server)
{
  guint resource_id = 0;
  SnraHttpResource *resource;

  if (!sscanf(path, "/resource/%u", &resource_id))
    goto error;

  resource = snra_server_get_resource(server, resource_id);
  if (resource == NULL)
    goto error;

  g_print ("Hit on resource %u\n", resource_id);
  snra_http_resource_new_transfer (resource, msg);
  
  return;
error:
  soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
}

static void
server_ui_cb (G_GNUC_UNUSED SoupServer *soup, SoupMessage *msg, 
  const char *path, G_GNUC_UNUSED GHashTable *query,
  G_GNUC_UNUSED SoupClientContext *client, G_GNUC_UNUSED SnraServer *server)
{
  const gchar *file_path;
  gchar *filename = NULL;
  gchar *contents;
  gsize size;
  const gchar *mime_type;

  if (!g_str_has_prefix (path, "/ui"))
    goto fail;

  file_path = path + 3;

  if (strstr (file_path, "/.."))
    goto fail;

  if (g_str_equal (file_path, "")) {
    snra_soup_message_set_redirect (msg, SOUP_STATUS_MOVED_PERMANENTLY, "/ui/");
    return;
  }

  if (g_str_equal (file_path, "/"))
    file_path = "/index.html";

  filename = g_strdup_printf ("src/static%s", file_path);

  g_print ("looking for %s\n", filename);

  if (!g_file_get_contents (filename, &contents, &size, NULL))
    goto fail;

  mime_type = snra_resource_get_mime_type (filename);
  g_print ("Returning %s - %s\n", mime_type, filename);

  soup_message_set_response (msg, mime_type, SOUP_MEMORY_TAKE, contents, size);
  soup_message_set_status (msg, SOUP_STATUS_OK);

  g_free (filename);
  return;

fail:
  g_free (filename);
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
  server->stream_time = GST_CLOCK_TIME_NONE;
  server->port = 5457;
  server->current_volume = 0.1;

  server->soup = soup_server_new(SOUP_SERVER_PORT, server->port, NULL);
  soup_server_add_handler (server->soup, "/", (SoupServerCallback) server_fallback_cb, g_object_ref (server), g_object_unref);
  soup_server_add_handler (server->soup, "/ui", (SoupServerCallback) server_ui_cb, g_object_ref (server), g_object_unref);
  soup_server_add_handler (server->soup, "/client", (SoupServerCallback) server_client_cb, g_object_ref (server), g_object_unref);
  soup_server_add_handler (server->soup, "/resource", (SoupServerCallback) server_resource_cb, g_object_ref (server), g_object_unref);
  socket = soup_server_get_listener(server->soup);
  if (socket) {
    SoupAddress *addr;
    g_object_get (socket, SOUP_SOCKET_LOCAL_ADDRESS, &addr, NULL);
    g_print ("Now listening on %s:%u\n", soup_address_get_name(addr), soup_address_get_port(addr));
    g_object_unref (addr);
  }

  server->resources = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);

  server->current_resource = 0;
}

static void
snra_server_class_init (SnraServerClass *server_class)
{
  GObjectClass *gobject_class = (GObjectClass *)(server_class);

  gobject_class->dispose = snra_server_dispose;
  gobject_class->finalize = snra_server_finalize;
  gobject_class->set_property = snra_server_set_property;
  gobject_class->get_property = snra_server_get_property;

  g_object_class_install_property (gobject_class, PROP_PORT,
    g_param_spec_int ("port", "port",
                         "port for Sonarea service",
                         1, 65535, 5457,
                         G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_RTSP_PORT,
    g_param_spec_int ("rtsp-port", "RTSP port",
                         "port for RTSP service",
                         1, 65535, 5458,
                         G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_CLOCK,
    g_param_spec_object ("clock", "clock",
                         "clock to synchronise playback",
                         GST_TYPE_NET_TIME_PROVIDER,
                         G_PARAM_READWRITE));
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
free_client_connection (gpointer data)
{
  SnraClientConnection *client = (SnraClientConnection *)(data);
  soup_message_body_complete (client->msg->response_body);
  g_free (client);
}

static void
snra_server_dispose(GObject *object)
{
  SnraServer *server = (SnraServer *)(object);

#if GLIB_CHECK_VERSION(2,28,0)
  g_list_free_full (server->clients, free_client_connection);
#else
  g_list_foreach (server->clients, (GFunc) free_client_connection, NULL);
  g_list_free (server->clients);
#endif
  server->clients = NULL;

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

void
snra_server_start (SnraServer *server)
{
  soup_server_run_async (server->soup);
}

void
snra_server_stop (SnraServer *server)
{
  soup_server_quit (server->soup);
}

void
snra_server_set_resource_cb (SnraServer *server, 
  SnraHttpResource *(*callback)(SnraServer *server, guint resource_id, void *cb_data), void *userdata)
{
  server->get_resource = callback;
  server->get_resource_userdata = userdata;
}

void
snra_server_add_handler (SnraServer *server, const gchar *path, SoupServerCallback callback, gpointer user_data,
    GDestroyNotify destroy_notify)
{
  soup_server_add_handler (server->soup, path, callback, user_data, destroy_notify);
}

void
snra_server_play_resource (SnraServer *server, guint resource_id)
{
  server->current_resource = resource_id;
  server->base_time = GST_CLOCK_TIME_NONE;
  server->stream_time = GST_CLOCK_TIME_NONE;
  server_send_play_media_msg (server, NULL, resource_id);
}

void
snra_server_send_play (SnraServer *server, SnraClientConnection *client)
{
  GstClock *clock;
  GstStructure *msg;

  /* Update base time to match length of time paused */
  g_object_get (server->net_clock, "clock", &clock, NULL);
  server->base_time = gst_clock_get_time (clock) + (GST_SECOND / 4) - server->stream_time;
  gst_object_unref (clock);
  server->stream_time = GST_CLOCK_TIME_NONE;

  msg = gst_structure_new ("json",
            "msg-type", G_TYPE_STRING, "play",
            "base-time", G_TYPE_INT64, (gint64)(server->base_time),
            NULL);

  server_send_msg_to_client (server, client, msg);
}

void
snra_server_send_pause (SnraServer *server, SnraClientConnection *client)
{
  GstClock *clock;
  GstStructure *msg;

  msg = gst_structure_new ("json",
            "msg-type", G_TYPE_STRING, "pause",
            NULL);

  server_send_msg_to_client (server, client, msg);

  if (server->stream_time == GST_CLOCK_TIME_NONE) {
    g_object_get (server->net_clock, "clock", &clock, NULL);
    /* Calculate how much of the current file we played up until now, and store */
    server->stream_time = gst_clock_get_time (clock) + (GST_SECOND / 4) - server->base_time;
    gst_object_unref (clock);
    g_print ("Storing stream_time %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (server->stream_time));
  }
}

void
snra_server_send_volume (SnraServer *server, SnraClientConnection *client, gdouble volume)
{
  GstStructure *msg;

  server->current_volume = volume;

  msg = gst_structure_new ("json",
            "msg-type", G_TYPE_STRING, "volume",
            "level", G_TYPE_DOUBLE, volume,
            NULL);

  server_send_msg_to_client (server, client, msg);
}
