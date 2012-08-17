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
 * Aurena Manager is the central object which:
 *   creates the network clock
 *   Establishes libsoup session
 *   Creates RTSP sessions as needed
 *   Distributes the network clock and base time to clients
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <json-glib/json-glib.h>

#include <src/snra-json.h>

#include "snra-config.h"
#include "snra-http-resource.h"
#include "snra-manager.h"
#include "snra-media-db.h"
#include "snra-server.h"
#include "snra-server-client.h"

enum
{
  PROP_0,
  PROP_CONFIG,
  PROP_LAST
};

G_DEFINE_TYPE (SnraManager, snra_manager, G_TYPE_OBJECT);

static void snra_manager_dispose (GObject * object);
static void snra_manager_finalize (GObject * object);
static SnraHttpResource *snra_manager_get_resource_cb (SnraServer * server,
    guint resource_id, void *userdata);
static void snra_manager_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void snra_manager_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void snra_manager_play_resource (SnraManager * manager,
    guint resource_id);
static void snra_manager_send_play (SnraManager * manager,
    SnraServerClient * client);
static void snra_manager_send_pause (SnraManager * manager,
    SnraServerClient * client);
static void snra_manager_send_volume (SnraManager * manager,
    SnraServerClient * client, gdouble volume);
static GstStructure *manager_make_set_media_msg (SnraManager *manager,
    guint resource_id);

static void
manager_send_msg_to_client (SnraManager * manager, SnraServerClient * client,
    GstStructure * msg);

static GstNetTimeProvider *
create_net_clock ()
{
  GstClock *clock;
  GstNetTimeProvider *net_time;

  clock = gst_system_clock_obtain ();
  net_time = gst_net_time_provider_new (clock, NULL, 0);
  gst_object_unref (clock);

  return net_time;
}

#ifdef HAVE_GST_RTSP
static GstRTSPServer *
create_rtsp_server (G_GNUC_UNUSED SnraManager * mgr)
{
  GstRTSPServer *server = NULL;

  server = gst_rtsp_server_new ();
  gst_rtsp_server_set_service (server, "5458");

  /* attach the server to the default maincontext */
  if (gst_rtsp_server_attach (server, NULL) == 0)
    goto failed;

  /* start serving */
  return server;

  /* ERRORS */
failed:
  {
    g_print ("failed to attach the server\n");
    gst_object_unref (server);
    return NULL;
  }
}
#endif

static GstStructure *
manager_make_enrol_msg (SnraManager * manager)
{
  int clock_port;
  GstClock *clock;
  GstClockTime cur_time;
  GstStructure *msg;

  g_object_get (manager->net_clock, "clock", &clock, NULL);
  cur_time = gst_clock_get_time (clock);
  gst_object_unref (clock);

  g_object_get (manager->net_clock, "port", &clock_port, NULL);

  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "enrol",
      "clock-port", G_TYPE_INT, clock_port,
      "current-time", G_TYPE_INT64, (gint64) (cur_time),
      "volume-level", G_TYPE_DOUBLE, manager->current_volume, NULL);

  return msg;
}

typedef enum _SnraControlEvent SnraControlEvent;

enum _SnraControlEvent
{
  SNRA_CONTROL_NONE,
  SNRA_CONTROL_NEXT,
  SNRA_CONTROL_PREV,
  SNRA_CONTROL_PLAY,
  SNRA_CONTROL_PAUSE,
  SNRA_CONTROL_ENQUEUE,
  SNRA_CONTROL_VOLUME
};

static const struct
{
  const char *name;
  SnraControlEvent type;
} control_event_names[] = {
  {
  "next", SNRA_CONTROL_NEXT}, {
  "previous", SNRA_CONTROL_PREV}, {
  "play", SNRA_CONTROL_PLAY}, {
  "pause", SNRA_CONTROL_PAUSE}, {
  "enqueue", SNRA_CONTROL_ENQUEUE}, {
  "volume", SNRA_CONTROL_VOLUME}
};

static const gint N_CONTROL_EVENTS = G_N_ELEMENTS (control_event_names);

#if 0
static gint
find_client_by_pipe (SnraServerClient * client, SoupMessage * wanted)
{
  if (client->event_pipe == wanted)
    return 0;
  return 1;
}
#endif

static gint
find_client_by_id (SnraServerClient * client, void *wanted_id)
{
  guint client_id = GPOINTER_TO_INT (wanted_id);

  if (client->client_id == client_id)
    return 0;
  return 1;
}

SnraServerClient *
snra_manager_get_player_client (SnraManager * manager, guint client_id)
{
  GList *item =
      g_list_find_custom (manager->player_clients, GINT_TO_POINTER (client_id),
      (GCompareFunc) (find_client_by_id));

  if (item == NULL)
    return NULL;

  return (SnraServerClient *) (item->data);
}

static void
manager_player_client_disconnect (SnraServerClient *client, SnraManager * manager)
{
  GList *item = g_list_find (manager->player_clients, client);
  g_print ("Disconnect from player client %u\n", client->client_id);
  if (item) {
    g_print ("Removing player client %u\n", client->client_id);
    g_object_unref (client);
    manager->player_clients =
        g_list_delete_link (manager->player_clients, item);
  }
}

static void
manager_ctrl_client_disconnect (SnraServerClient *client, SnraManager * manager)
{
  GList *item = g_list_find (manager->ctrl_clients, client);
  g_print ("Disconnect from control client %u\n", client->client_id);
  if (item) {
    g_print ("Removing control client %u\n", client->client_id);
    g_object_unref (client);
    manager->ctrl_clients = g_list_delete_link (manager->ctrl_clients, item);
  }
}

static void
manager_client_cb (SoupServer * soup, SoupMessage * msg,
    G_GNUC_UNUSED const char *path, G_GNUC_UNUSED GHashTable * query,
    G_GNUC_UNUSED SoupClientContext * client, SnraManager * manager)
{
  SnraServerClient *client_conn = g_new0 (SnraServerClient, 1);
  gchar **parts = g_strsplit (path, "/", 3);
  guint n_parts = g_strv_length (parts);

  if (n_parts < 3 || !g_str_equal ("client", parts[1]))
    goto done;                  /* Invalid request */

  /* Check if the request is a websocket request, if not handle as chunked */
  client_conn = snra_server_client_new (soup, msg, client);

  if (g_str_equal (parts[2], "player")) {
    g_signal_connect (client_conn, "connection-lost",
        G_CALLBACK (manager_player_client_disconnect), manager);
    manager->player_clients =
        g_list_prepend (manager->player_clients, client_conn);
  } else if (g_str_equal (parts[2], "control")) {
    g_signal_connect (client_conn, "connection-lost",
        G_CALLBACK (manager_ctrl_client_disconnect), manager);
    manager->ctrl_clients = g_list_prepend (manager->ctrl_clients, client_conn);
  }

  manager_send_msg_to_client (manager, client_conn,
      manager_make_enrol_msg (manager));

  if (manager->current_resource) {
    manager_send_msg_to_client (manager, client_conn,
        manager_make_set_media_msg (manager, manager->current_resource));
  }

done:
  g_strfreev (parts);
}

static void
manager_send_msg_to_client (SnraManager * manager, SnraServerClient * client,
    GstStructure *msg)
{
  JsonGenerator *gen;
  JsonNode *root;
  gchar *body;
  gsize len;

  root = snra_json_from_gst_structure (msg);
  gst_structure_free (msg);

  gen = json_generator_new ();

  json_generator_set_root (gen, root);

  body = json_generator_to_data (gen, &len);

  g_object_unref (gen);
  json_node_free (root);

  if (client) {
    snra_server_client_send_message (client, body, len);
  } else {
    /* client == NULL - send to all clients */
    GList *cur;
    for (cur = manager->player_clients; cur != NULL; cur = g_list_next (cur)) {
      client = (SnraServerClient *) (cur->data);
      snra_server_client_send_message (client, body, len);
    }
    for (cur = manager->ctrl_clients; cur != NULL; cur = g_list_next (cur)) {
      client = (SnraServerClient *) (cur->data);
      snra_server_client_send_message (client, body, len);
    }
  }
  g_free (body);
}

static SnraControlEvent
str_to_control_event_type (const gchar * str)
{
  gint i;
  for (i = 0; i < N_CONTROL_EVENTS; i++) {
    if (g_str_equal (str, control_event_names[i].name))
      return control_event_names[i].type;
  }

  return SNRA_CONTROL_NONE;
}

static guint
get_playlist_len (SnraManager * mgr)
{
  return snra_media_db_get_file_count (mgr->media_db);
}

static void
control_callback (G_GNUC_UNUSED SoupServer * soup, SoupMessage * msg,
    const char *path, GHashTable * query,
    G_GNUC_UNUSED SoupClientContext * client, SnraManager * manager)
{
  gchar **parts = g_strsplit (path, "/", 3);
  guint n_parts = g_strv_length (parts);
  SnraControlEvent event_type;
  GHashTable *post_params = NULL;
  const gchar *content_type;

  if (n_parts < 3 || !g_str_equal ("control", parts[1]))
    goto done;                  /* Invalid request */

  event_type = str_to_control_event_type (parts[2]);
  content_type =
      soup_message_headers_get_content_type (msg->request_headers, NULL);
  if (g_str_equal (msg->method, "POST") &&
      content_type &&
      g_str_equal (content_type, SOUP_FORM_MIME_TYPE_URLENCODED))
    post_params = soup_form_decode (msg->request_body->data);

  switch (event_type) {
    case SNRA_CONTROL_NEXT:{
      gchar *id_str = NULL;
      guint resource_id;

      if (query)
        id_str = g_hash_table_lookup (query, "id");

      if (id_str == NULL || !sscanf (id_str, "%d", &resource_id)) {
        /* No or invalid resource id: skip to another random track */
        resource_id =
            (guint) g_random_int_range (0, get_playlist_len (manager)) + 1;
      } else {
        resource_id = CLAMP (resource_id, 1, get_playlist_len (manager));
      }
      manager->paused = FALSE;
      snra_manager_play_resource (manager, resource_id);
      break;
    }
    case SNRA_CONTROL_PAUSE:{
      if (!manager->paused)
        snra_manager_send_pause (manager, NULL);
      manager->paused = TRUE;
      break;
    }
    case SNRA_CONTROL_PLAY:{
      if (manager->paused) {
        manager->paused = FALSE;
        if (manager->current_resource == 0) {
          snra_manager_play_resource (manager,
              g_random_int_range (0, get_playlist_len (manager) + 1));
        }
        else {
          snra_manager_send_play (manager, NULL);
        }
      }
      break;
    }
    case SNRA_CONTROL_VOLUME:{
      gchar *vol_str = NULL;
      gdouble new_vol;
      if (query)
        vol_str = g_hash_table_lookup (query, "level");
      if (vol_str == NULL && post_params)
        vol_str = g_hash_table_lookup (post_params, "level");

      if (vol_str && sscanf (vol_str, "%lf", &new_vol)) {
        new_vol = CLAMP (new_vol, 0.0, 10.0);
        snra_manager_send_volume (manager, NULL, new_vol);
      }

      break;
    }
    default:
      g_message ("Ignoring unknown/unimplemented control %s\n", parts[2]);
      soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
      goto done;
  }

  soup_message_set_response (msg, "text/plain", SOUP_MEMORY_STATIC, " ", 1);
  soup_message_set_status (msg, SOUP_STATUS_OK);
done:
  if (post_params)
    g_hash_table_destroy (post_params);
  g_strfreev (parts);
}


static void
snra_manager_init (SnraManager * manager)
{
  manager->playlist = g_ptr_array_new ();
  manager->net_clock = create_net_clock ();
  manager->paused = TRUE;

  manager->avahi = g_object_new (SNRA_TYPE_AVAHI, NULL);

  manager->base_time = GST_CLOCK_TIME_NONE;
  manager->stream_time = GST_CLOCK_TIME_NONE;
  manager->current_volume = 0.1;

  manager->current_resource = 0;
}

static void
snra_manager_constructed (GObject * object)
{
  SnraManager *manager = (SnraManager *) (object);
  gchar *db_file;

  if (G_OBJECT_CLASS (snra_manager_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (snra_manager_parent_class)->constructed (object);

#ifdef HAVE_GST_RTSP
  manager->rtsp = create_rtsp_server (manager);
#endif

  manager->server = g_object_new (SNRA_TYPE_SERVER,
      "config", manager->config, NULL);
  snra_server_set_resource_cb (manager->server,
      snra_manager_get_resource_cb, manager);
  snra_server_add_handler (manager->server, "/control",
      (SoupServerCallback) control_callback,
      g_object_ref (G_OBJECT (manager)), g_object_unref);
  snra_server_add_handler (manager->server, "/client",
      (SoupServerCallback) manager_client_cb,
      g_object_ref (manager), g_object_unref);

  g_object_get (manager->config, "database", &db_file, NULL);
  manager->media_db = snra_media_db_new (db_file);
  g_free (db_file);
}

static void
snra_manager_class_init (SnraManagerClass * manager_class)
{
  GObjectClass *object_class = (GObjectClass *) (manager_class);

  object_class->constructed = snra_manager_constructed;
  object_class->set_property = snra_manager_set_property;
  object_class->get_property = snra_manager_get_property;
  object_class->dispose = snra_manager_dispose;
  object_class->finalize = snra_manager_finalize;

  g_object_class_install_property (object_class, PROP_CONFIG,
      g_param_spec_object ("config", "config",
          "Aurena service configuration object",
          SNRA_TYPE_CONFIG, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
snra_manager_dispose (GObject * object)
{
  SnraManager *manager = (SnraManager *) (object);

  g_list_foreach (manager->ctrl_clients, (GFunc) g_object_unref, NULL);
  g_list_free (manager->ctrl_clients);
  manager->ctrl_clients = NULL;

  g_list_foreach (manager->player_clients, (GFunc) g_object_unref, NULL);
  g_list_free (manager->player_clients);
  manager->player_clients = NULL;

  G_OBJECT_CLASS (snra_manager_parent_class)->dispose (object);
}

static void
snra_manager_finalize (GObject * object)
{
  SnraManager *manager = (SnraManager *) (object);

  g_ptr_array_foreach (manager->playlist, (GFunc) g_free, NULL);
  g_ptr_array_free (manager->playlist, TRUE);

  g_object_unref (manager->config);
  g_object_unref (manager->media_db);

  snra_server_stop (manager->server);
  g_object_unref (manager->server);
}

static void
snra_manager_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  SnraManager *manager = (SnraManager *) (object);

  switch (prop_id) {
    case PROP_CONFIG:
      manager->config = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
snra_manager_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  SnraManager *manager = (SnraManager *) (object);

  switch (prop_id) {
    case PROP_CONFIG:
      g_value_set_object (value, manager->config);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#ifdef HAVE_GST_RTSP
static void
rtsp_media_prepared (GstRTSPMedia * media, G_GNUC_UNUSED SnraManager * mgr)
{
  g_object_set (media->rtpbin, "use-pipeline-clock", TRUE, NULL);
}

static void
new_stream_constructed_cb (G_GNUC_UNUSED GstRTSPMediaFactory * factory,
    GstRTSPMedia * media, SnraManager * mgr)
{
  g_print ("Media constructed: %p\n", media);
  g_signal_connect (media, "prepared", G_CALLBACK (rtsp_media_prepared), mgr);
}

static void
add_rtsp_uri (SnraManager * manager, guint resource_id,
    const gchar * source_uri)
{
  GstRTSPMediaMapping *mapping;
  GstRTSPMediaFactoryURI *factory;
  gchar *rtsp_uri = g_strdup_printf ("/resource/%u", resource_id);

  mapping = gst_rtsp_server_get_media_mapping (manager->rtsp);
  factory = gst_rtsp_media_factory_uri_new ();
  /* Set up the URI, and set as shared (all viewers see the same stream) */
  gst_rtsp_media_factory_uri_set_uri (factory, source_uri);
  gst_rtsp_media_factory_set_shared (GST_RTSP_MEDIA_FACTORY (factory), TRUE);
  g_signal_connect (factory, "media-constructed",
      G_CALLBACK (new_stream_constructed_cb), manager);
  /* attach the test factory to the test url */
  gst_rtsp_media_mapping_add_factory (mapping, rtsp_uri,
      GST_RTSP_MEDIA_FACTORY (factory));
  g_object_unref (mapping);

  g_free (rtsp_uri);
}
#endif

static void
read_playlist_file (SnraManager * manager, const char *filename)
{
  GError *error = NULL;
  GIOChannel *io = g_io_channel_new_file (filename, "r", &error);
  GIOStatus result;
  gchar *line;

  if (error) {
    g_message ("Failed to open playlist file %s: %s", filename, error->message);
    g_error_free (error);
    return;
  }

  do {
    result = g_io_channel_read_line (io, &line, NULL, NULL, NULL);
    if (result == G_IO_STATUS_AGAIN)
      continue;
    if (result != G_IO_STATUS_NORMAL)
      break;
    g_strchomp (line);
    g_ptr_array_add (manager->playlist, line);
    snra_media_db_add_file (manager->media_db, line);
  } while (TRUE);

  g_print ("Read %u entries\n", manager->playlist->len);

  g_io_channel_unref (io);
}


SnraManager *
snra_manager_new (const char *config_file)
{
  SnraConfig *config;
  SnraManager *manager;
  gchar *playlist_file;

  config = snra_config_new (config_file);
  if (config == NULL)
    return NULL;

  manager = g_object_new (SNRA_TYPE_MANAGER, "config", config, NULL);

  g_object_get (config, "playlist", &playlist_file, NULL);
  if (playlist_file) {
    read_playlist_file (manager, playlist_file);
    g_free (playlist_file);
  }

  if (get_playlist_len (manager)) {
#ifdef HAVE_GST_RTSP
    char *rtsp_uri = g_strdup_printf ("file://%s",
        (gchar *) (g_ptr_array_index (manager->playlist, 0)));
    add_rtsp_uri (manager, 1, rtsp_uri);
    g_free (rtsp_uri);
#endif

  }

  snra_server_start (manager->server);

  return manager;
}

static SnraHttpResource *
snra_manager_get_resource_cb (G_GNUC_UNUSED SnraServer * server,
    guint resource_id, void *userdata)
{
  SnraManager *manager = (SnraManager *) (userdata);
  SnraHttpResource *ret;
  gchar *file;

  if (resource_id < 1 || resource_id > get_playlist_len (manager))
    return NULL;

  file = snra_media_db_get_file_by_id (manager->media_db, resource_id);
  if (file == NULL)
    return NULL;

  g_print ("Creating resource %u for %s\n", resource_id, file);

  ret = g_object_new (SNRA_TYPE_HTTP_RESOURCE, "source-path", file, NULL);
  g_free (file);

  return ret;
}

static void
snra_manager_play_resource (SnraManager * manager, guint resource_id)
{
  manager->current_resource = resource_id;
  manager->base_time = GST_CLOCK_TIME_NONE;
  manager->stream_time = GST_CLOCK_TIME_NONE;

  manager_send_msg_to_client (manager, NULL,
      manager_make_set_media_msg (manager, manager->current_resource));
}

static void
snra_manager_send_play (SnraManager * manager, SnraServerClient * client)
{
  GstClock *clock;
  GstStructure *msg;

  /* Update base time to match length of time paused */
  g_object_get (manager->net_clock, "clock", &clock, NULL);
  manager->base_time =
      gst_clock_get_time (clock) + (GST_SECOND / 4) - manager->stream_time;
  gst_object_unref (clock);
  manager->stream_time = GST_CLOCK_TIME_NONE;

  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "play",
      "base-time", G_TYPE_INT64, (gint64) (manager->base_time), NULL);

  manager_send_msg_to_client (manager, client, msg);
}

static void
snra_manager_send_pause (SnraManager * manager, SnraServerClient * client)
{
  GstClock *clock;
  GstStructure *msg;

  msg = gst_structure_new ("json", "msg-type", G_TYPE_STRING, "pause", NULL);

  manager_send_msg_to_client (manager, client, msg);

  if (manager->stream_time == GST_CLOCK_TIME_NONE) {
    g_object_get (manager->net_clock, "clock", &clock, NULL);
    /* Calculate how much of the current file we played up until now, and store */
    manager->stream_time =
        gst_clock_get_time (clock) + (GST_SECOND / 4) - manager->base_time;
    gst_object_unref (clock);
    g_print ("Storing stream_time %" GST_TIME_FORMAT "\n",
        GST_TIME_ARGS (manager->stream_time));
  }
}

static void
snra_manager_send_volume (SnraManager * manager, SnraServerClient * client,
    gdouble volume)
{
  GstStructure *msg;

  manager->current_volume = volume;

  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "volume",
      "level", G_TYPE_DOUBLE, volume, NULL);

  manager_send_msg_to_client (manager, client, msg);
}

static GstStructure *
manager_make_set_media_msg (SnraManager * manager, guint resource_id)
{
  GstClock *clock;
  GstClockTime cur_time;
  gchar *resource_path;
  GstStructure *msg;
  gint port;

  g_object_get (manager->net_clock, "clock", &clock, NULL);
  cur_time = gst_clock_get_time (clock);
  gst_object_unref (clock);

  resource_path = g_strdup_printf ("/resource/%u", resource_id);

  if (manager->base_time == GST_CLOCK_TIME_NONE) {
    // configure a base time 0.25 seconds in the future
    manager->base_time = cur_time + (GST_SECOND / 4);
    manager->stream_time = GST_CLOCK_TIME_NONE;
    g_print ("Base time now %" G_GUINT64_FORMAT "\n", manager->base_time);
  }
#if 1
  g_object_get (manager->config, "snra-port", &port, NULL);
#else
  g_object_get (manager->config, "rtsp-port", &port, NULL);
#endif

  msg = gst_structure_new ("json", "msg-type", G_TYPE_STRING, "set-media",
#if 1
      "resource-protocol", G_TYPE_STRING, "http",
      "resource-port", G_TYPE_INT, port,
#else
      "resource-protocol", G_TYPE_STRING, "rtsp",
      "resource-port", G_TYPE_INT, port,
#endif
      "resource-path", G_TYPE_STRING, resource_path,
      "base-time", G_TYPE_INT64, (gint64) (manager->base_time),
      "paused", G_TYPE_BOOLEAN, manager->paused, NULL);

  g_free (resource_path);

  return msg;
}
