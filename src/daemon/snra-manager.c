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

typedef struct _SnraPlayerInfo SnraPlayerInfo;
struct _SnraPlayerInfo
{
  guint id;
  gchar *host;
  SnraServerClient *conn;

  gdouble volume;
  gboolean enabled;
};

typedef enum _SnraControlEvent SnraControlEvent;
enum _SnraControlEvent
{
  SNRA_CONTROL_NONE,
  SNRA_CONTROL_NEXT,
  SNRA_CONTROL_PREV,
  SNRA_CONTROL_PLAY,
  SNRA_CONTROL_PAUSE,
  SNRA_CONTROL_ENQUEUE,
  SNRA_CONTROL_VOLUME,
  SNRA_CONTROL_CLIENT_SETTING
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
  "volume", SNRA_CONTROL_VOLUME}, {
  "setclient", SNRA_CONTROL_CLIENT_SETTING}
};

static const gint N_CONTROL_EVENTS = G_N_ELEMENTS (control_event_names);

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
static void snra_manager_adjust_volume (SnraManager * manager,
    gdouble volume);
static void snra_manager_adjust_client_volume (SnraManager * manager,
    guint client_id, gdouble volume);
static void snra_manager_adjust_client_setting (SnraManager * manager,
    guint client_id, gboolean enable);
static GstStructure *manager_make_set_media_msg (SnraManager * manager,
    guint resource_id);
static GstStructure *manager_make_player_clients_changed_msg
    (SnraManager * manager);
static SnraPlayerInfo *
get_player_info_by_id (SnraManager *manager, guint client_id);

#define SEND_MSG_TO_PLAYERS 1
#define SEND_MSG_TO_DISABLED_PLAYERS 2
#define SEND_MSG_TO_ENABLED_PLAYERS 4
#define SEND_MSG_TO_CONTROLLERS 8
#define SEND_MSG_TO_ALL (SEND_MSG_TO_PLAYERS|SEND_MSG_TO_CONTROLLERS)

static void
manager_send_msg_to_client (SnraManager * manager, SnraServerClient * client,
    gint send_to_mask, GstStructure * msg);

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
manager_make_enrol_msg (SnraManager * manager, SnraPlayerInfo *info)
{
  int clock_port;
  GstClock *clock;
  GstClockTime cur_time;
  GstStructure *msg;
  gdouble volume = manager->current_volume;

  g_object_get (manager->net_clock, "clock", &clock, NULL);
  cur_time = gst_clock_get_time (clock);
  gst_object_unref (clock);

  g_object_get (manager->net_clock, "port", &clock_port, NULL);

  if (info != NULL) /* Is a player message */
    volume *= info->volume;

  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "enrol",
      "resource-id", G_TYPE_INT64, (gint64) manager->current_resource,
      "clock-port", G_TYPE_INT, clock_port,
      "current-time", G_TYPE_INT64, (gint64) (cur_time),
      "volume-level", G_TYPE_DOUBLE, volume,
      "paused", G_TYPE_BOOLEAN, manager->paused, NULL);

  if (info != NULL) /* Is a player message */
    gst_structure_set (msg, "enabled", G_TYPE_BOOLEAN, info->enabled, NULL);

  return msg;
}

static GstStructure *
make_player_clients_list_msg (SnraManager * manager)
{
  GstStructure *msg;
  GValue p = G_VALUE_INIT;
  GList *cur;

  g_value_init (&p, GST_TYPE_ARRAY);

  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "player-clients", NULL);

  for (cur = manager->player_info; cur != NULL; cur = g_list_next (cur)) {
    SnraPlayerInfo *info = (SnraPlayerInfo *) (cur->data);
    if (info->conn != NULL) {
      GValue tmp = G_VALUE_INIT;
      GstStructure *cur_struct = gst_structure_new ("client",
          "client-id", G_TYPE_INT64, (gint64) info->id,
          "enabled", G_TYPE_BOOLEAN, info->enabled,
          "volume", G_TYPE_DOUBLE, info->volume,
          "host", G_TYPE_STRING, info->host,
          NULL);

      g_value_init (&tmp, GST_TYPE_STRUCTURE);
      gst_value_set_structure (&tmp, cur_struct);
      gst_value_array_append_value (&p, &tmp);
      g_value_unset (&tmp);
      gst_structure_free (cur_struct);
    }
  }
  gst_structure_take_value (msg, "player-clients", &p);

  return msg;
}

static gint
find_player_info_by_client (const SnraPlayerInfo *info, SnraServerClient *client)
{
  if (info->conn == client)
    return 0;

  return -1;
}

static void
manager_player_client_disconnect (SnraServerClient * client,
    SnraManager * manager)
{
  GList *item = g_list_find_custom (manager->player_info, client, (GCompareFunc)(find_player_info_by_client));
  if (item) {
    SnraPlayerInfo *info = (SnraPlayerInfo *)(item->data);

    g_print ("Disconnecting player client %u\n", info->id);

    g_object_unref (client);
    info->conn = NULL;

    manager_send_msg_to_client (manager, NULL, SEND_MSG_TO_CONTROLLERS,
        manager_make_player_clients_changed_msg (manager));
  }
}

static void
manager_ctrl_client_disconnect (SnraServerClient * client,
    SnraManager * manager)
{
  GList *item = g_list_find (manager->ctrl_clients, client);
  if (item) {
    g_print ("Removing control client %u\n", client->conn_id);
    g_object_unref (client);
    manager->ctrl_clients = g_list_delete_link (manager->ctrl_clients, item);
  }
}

static void
manager_status_client_disconnect (SnraServerClient * client,
    G_GNUC_UNUSED SnraManager * manager)
{
  g_object_unref (client);
}

static gboolean
handle_ping_timeout (SnraManager *manager)
{
  GstStructure *msg;

  if (manager->ping_timeout == 0)
    return FALSE;

  /* Send a ping to each client */
  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "ping", NULL);
  manager_send_msg_to_client (manager, NULL,
     SEND_MSG_TO_ALL, msg);

  return TRUE;
}

static void
send_enrol_events (SnraManager * manager, SnraServerClient * client,
    SnraPlayerInfo *info)
{
  manager_send_msg_to_client (manager, client, SEND_MSG_TO_ALL,
      manager_make_enrol_msg (manager, info));

  if (manager->current_resource) {
    manager_send_msg_to_client (manager, client, SEND_MSG_TO_ALL,
        manager_make_set_media_msg (manager, manager->current_resource));
  }
  if (info == NULL) {
    manager_send_msg_to_client (manager, client, SEND_MSG_TO_CONTROLLERS,
        manager_make_player_clients_changed_msg (manager));
  }

  if (manager->ping_timeout == 0) {
    manager->ping_timeout = g_timeout_add_seconds (2,  (GSourceFunc) handle_ping_timeout, manager);
  }
}

static gint
find_unlinked_player_info_by_host (const SnraPlayerInfo *p, const gchar *host)
{
  if (p->conn == NULL && g_str_equal (p->host, host))
    return 0;
  return -1;
}

static gint
find_player_info_by_client_id (const SnraPlayerInfo *p, const gpointer cid_ptr)
{
  const guint client_id = GPOINTER_TO_INT(cid_ptr);
  if (p->id == client_id)
    return 0;
  return -1;
}

static SnraPlayerInfo *
get_player_info_by_id (SnraManager *manager, guint client_id)
{
  GList *entry;
  SnraPlayerInfo *info = NULL;

  /* See if there's a player instance that matches */
  entry = g_list_find_custom (manager->player_info, GINT_TO_POINTER (client_id), (GCompareFunc) find_player_info_by_client_id);
  if (entry)
    info = (SnraPlayerInfo *)(entry->data);

  return info;
}

static SnraPlayerInfo *
get_player_info_for_client (SnraManager *manager, SnraServerClient *client)
{
  GList *entry;
  SnraPlayerInfo *info = NULL;
  const gchar *host;

  /* See if there's a disconnected player instance that matches */
  host = snra_server_client_get_host (client);
  entry = g_list_find_custom (manager->player_info, host, (GCompareFunc) find_unlinked_player_info_by_host);
  if (entry == NULL) {
    info = g_new0 (SnraPlayerInfo, 1);
    /* Init the player info */
    info->host = g_strdup (host);
    info->id = manager->next_player_id++;
    info->volume = 1.0;
    /* FIXME: Disable new clients if playing, otherwise enable */
    info->enabled = manager->paused;
    manager->player_info = entry = g_list_prepend (manager->player_info, info);

    g_print ("New player id %u\n", info->id);
  }
  else {
    info = (SnraPlayerInfo *)(entry->data);
    g_print ("Player id %u rejoining\n", info->id);
  }

  info->conn = client;

  return info;
}

static void
manager_client_cb (SoupServer * soup, SoupMessage * msg,
    G_GNUC_UNUSED const char *path, G_GNUC_UNUSED GHashTable * query,
    G_GNUC_UNUSED SoupClientContext *ctx, SnraManager * manager)
{
  SnraServerClient *client_conn = NULL;
  gchar **parts = g_strsplit (path, "/", 3);
  guint n_parts = g_strv_length (parts);

  if (n_parts < 3 || !g_str_equal ("client", parts[1])) {
    soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
    goto done;                  /* Invalid request */
  }

  if (g_str_equal (parts[2], "player_events")) {
    SnraPlayerInfo *info;

    client_conn = snra_server_client_new (soup, msg, ctx);
    g_signal_connect (client_conn, "connection-lost",
        G_CALLBACK (manager_player_client_disconnect), manager);

    info = get_player_info_for_client (manager, client_conn);

    send_enrol_events (manager, client_conn, info);
    manager_send_msg_to_client (manager, NULL, SEND_MSG_TO_CONTROLLERS,
        manager_make_player_clients_changed_msg (manager));
  } else if (g_str_equal (parts[2], "control_events")) {
    client_conn = snra_server_client_new (soup, msg, ctx);
    g_signal_connect (client_conn, "connection-lost",
        G_CALLBACK (manager_ctrl_client_disconnect), manager);
    manager->ctrl_clients = g_list_prepend (manager->ctrl_clients, client_conn);
    send_enrol_events (manager, client_conn, NULL);
  } else if (g_str_equal (parts[2], "player_info")) {
    client_conn = snra_server_client_new_single (soup, msg, ctx);
    g_signal_connect (client_conn, "connection-lost",
        G_CALLBACK (manager_status_client_disconnect), manager);
    manager_send_msg_to_client (manager, client_conn, 0,
        make_player_clients_list_msg (manager));
  } else {
    soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
  }

done:
  g_strfreev (parts);
}

static void
manager_send_msg_to_client (SnraManager * manager, SnraServerClient * client,
    gint send_to_mask, GstStructure * msg)
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
    if (send_to_mask & SEND_MSG_TO_PLAYERS) {
      for (cur = manager->player_info; cur != NULL; cur = g_list_next (cur)) {
        SnraPlayerInfo *info = (SnraPlayerInfo *) (cur->data);
        if (info->conn)
          snra_server_client_send_message (info->conn, body, len);
      }
    }
    if (send_to_mask & SEND_MSG_TO_CONTROLLERS) {
      for (cur = manager->ctrl_clients; cur != NULL; cur = g_list_next (cur)) {
        client = (SnraServerClient *) (cur->data);
        snra_server_client_send_message (client, body, len);
      }
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

static gchar *
find_param_str (const gchar * param_name, GHashTable * query_params,
    GHashTable * post_params)
{
  gchar *out = NULL;

  if (query_params)
    out = g_hash_table_lookup (query_params, param_name);
  if (out == NULL && post_params)
    out = g_hash_table_lookup (post_params, param_name);

  return out;
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
      gchar *id_str = find_param_str ("id", query, post_params);
      guint resource_id;

      if (get_playlist_len (manager) == 0) {
        resource_id = 0;
      } else if (id_str == NULL || !sscanf (id_str, "%d", &resource_id)) {
        /* No or invalid resource id: skip to next track */
        resource_id =
          (guint) (manager->current_resource % get_playlist_len (manager)) + 1;
      } else {
        resource_id = CLAMP (resource_id, 1, get_playlist_len (manager));
      }
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
        if (manager->current_resource == 0)
          snra_manager_play_resource (manager,
              get_playlist_len (manager) ? 1 : 0);
        else
          snra_manager_send_play (manager, NULL);
      }
      break;
    }
    case SNRA_CONTROL_VOLUME:{
      gchar *vol_str = find_param_str ("level", query, post_params);
      gchar *id_str = find_param_str ("client_id", query, post_params);
      guint client_id = 0;
      gdouble new_vol;

      if (id_str != NULL)
        sscanf (id_str, "%u", &client_id);

      if (vol_str) {
        new_vol = g_ascii_strtod (vol_str, NULL);
        new_vol = CLAMP (new_vol, 0.0, 10.0);
        if (client_id == 0)
          snra_manager_adjust_volume (manager, new_vol);
        else
          snra_manager_adjust_client_volume (manager, client_id, new_vol);
      }

      break;
    }
    case SNRA_CONTROL_CLIENT_SETTING:{
      gchar *set_str = find_param_str ("enable", query, post_params);
      gchar *id_str = find_param_str ("client_id", query, post_params);
      guint client_id = 0;
      gint enable = 1;

      if (id_str != NULL)
        sscanf (id_str, "%u", &client_id);

      if (set_str && sscanf (set_str, "%d", &enable)) {
        if (client_id > 0)
          snra_manager_adjust_client_setting (manager, client_id, enable != 0);
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

  manager->base_time = GST_CLOCK_TIME_NONE;
  manager->position = 0;
  manager->current_volume = 0.1;

  manager->current_resource = 0;
  manager->next_player_id = 1;
}

static void
snra_manager_constructed (GObject * object)
{
  SnraManager *manager = (SnraManager *) (object);
  gchar *db_file;
  int snra_port;

  if (G_OBJECT_CLASS (snra_manager_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (snra_manager_parent_class)->constructed (object);

#ifdef HAVE_GST_RTSP
  manager->rtsp = create_rtsp_server (manager);
#endif

  g_object_get (manager->config, "snra-port", &snra_port, NULL);

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

  manager->avahi = g_object_new (SNRA_TYPE_AVAHI, "snra-port", snra_port, NULL);

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
free_player_info (SnraPlayerInfo *info)
{
  if (info->conn)
    g_object_unref (info->conn);
  g_free (info->host);
  g_free (info);
}

static void
snra_manager_dispose (GObject * object)
{
  SnraManager *manager = (SnraManager *) (object);

  g_list_foreach (manager->ctrl_clients, (GFunc) g_object_unref, NULL);
  g_list_free (manager->ctrl_clients);
  manager->ctrl_clients = NULL;

  g_list_foreach (manager->player_info, (GFunc) free_player_info, NULL);
  g_list_free (manager->player_info);
  manager->player_info = NULL;

  if (manager->ping_timeout) {
    g_source_remove (manager->ping_timeout);
    manager->ping_timeout = 0;
  }

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
  manager->position = 0;

  if (manager->current_resource)
    manager_send_msg_to_client (manager, NULL, SEND_MSG_TO_ALL,
        manager_make_set_media_msg (manager, manager->current_resource));
}

static void
snra_manager_send_play (SnraManager * manager, SnraServerClient * client)
{
  GstClock *clock;
  GstStructure *msg;

  /* Update base time to match length of time paused */
  g_object_get (manager->net_clock, "clock", &clock, NULL);
  manager->base_time = gst_clock_get_time (clock) - manager->position + (GST_SECOND / 30);
  gst_object_unref (clock);
  manager->position = 0;

  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "play",
      "base-time", G_TYPE_INT64, (gint64) (manager->base_time), NULL);

  manager_send_msg_to_client (manager, client, SEND_MSG_TO_ALL, msg);
}

static void
snra_manager_send_pause (SnraManager * manager, SnraServerClient * client)
{
  GstClock *clock;
  GstClockTime now;
  GstStructure *msg;

  g_object_get (manager->net_clock, "clock", &clock, NULL);
  now = gst_clock_get_time (clock);
  gst_object_unref (clock);

  /* Calculate how much of the current file we played up until now, and store */
  manager->position = now - manager->base_time + (GST_SECOND / 30);
  g_print ("Storing position %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (manager->position));

  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "pause",
      "position", G_TYPE_INT64, (gint64) manager->position,
      NULL);

  manager_send_msg_to_client (manager, client, SEND_MSG_TO_ALL, msg);
}

static void
snra_manager_adjust_client_volume (SnraManager * manager, guint client_id,
    gdouble volume)
{
  GstStructure *msg = NULL;
  SnraPlayerInfo *info;

  info = get_player_info_by_id (manager, client_id);
  if (info == NULL)
    return;

  info->volume = volume;
  msg = gst_structure_new ("json",
          "msg-type", G_TYPE_STRING, "client-volume",
          "client-id", G_TYPE_INT64, (gint64) client_id,
          "level", G_TYPE_DOUBLE, volume, NULL);
  manager_send_msg_to_client (manager, NULL, SEND_MSG_TO_CONTROLLERS, msg);

  /* Tell the player which volume to set */
  msg = gst_structure_new ("json",
           "msg-type", G_TYPE_STRING, "volume",
           "level", G_TYPE_DOUBLE, volume * manager->current_volume, NULL);
  manager_send_msg_to_client (manager, info->conn, 0, msg);
}

static void
snra_manager_adjust_client_setting (SnraManager * manager, guint client_id,
    gboolean enable)
{
  GstStructure *msg = NULL;
  SnraPlayerInfo *info;

  info = get_player_info_by_id (manager, client_id);
  if (info == NULL)
    return;

  info->enabled = enable;
  msg = gst_structure_new ("json",
          "msg-type", G_TYPE_STRING, "client-setting",
          "client-id", G_TYPE_INT64, (gint64) client_id,
          "enabled", G_TYPE_BOOLEAN, enable, NULL);
  manager_send_msg_to_client (manager, NULL, SEND_MSG_TO_CONTROLLERS, msg);

  /* Tell the player which volume to set */
  msg = gst_structure_new ("json",
           "msg-type", G_TYPE_STRING, "client-setting",
           "enabled", G_TYPE_BOOLEAN, enable, NULL);
  manager_send_msg_to_client (manager, info->conn, 0, msg);
}

static void
snra_manager_adjust_volume (SnraManager * manager, gdouble volume)
{
  GstStructure *msg = NULL;
  GList *cur;

  manager->current_volume = volume;
  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "volume",
      "level", G_TYPE_DOUBLE, volume, NULL);
  manager_send_msg_to_client (manager, NULL, SEND_MSG_TO_CONTROLLERS, msg);

  /* Send a volume adjustment to each player */
  for (cur = manager->player_info; cur != NULL; cur = cur->next) {
    SnraPlayerInfo *info = (SnraPlayerInfo *)(cur->data);
    msg = gst_structure_new ("json",
           "msg-type", G_TYPE_STRING, "volume",
           "level", G_TYPE_DOUBLE, info->volume * volume, NULL);
    manager_send_msg_to_client (manager, info->conn, 0, msg);
  }
}

static GstStructure *
manager_make_set_media_msg (SnraManager * manager, guint resource_id)
{
  GstClock *clock;
  GstClockTime cur_time, position;
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
    manager->position = 0;
    g_print ("Base time now %" G_GUINT64_FORMAT "\n", manager->base_time);
  }
#if 1
  g_object_get (manager->config, "snra-port", &port, NULL);
#else
  g_object_get (manager->config, "rtsp-port", &port, NULL);
#endif

  /* Calculate position if currently playing */
  if (!manager->paused && cur_time > manager->base_time)
    position = cur_time - manager->base_time;
  else
    position = manager->position;

  msg = gst_structure_new ("json", "msg-type", G_TYPE_STRING, "set-media",
      "resource-id", G_TYPE_INT64, (gint64) resource_id,
#if 1
      "resource-protocol", G_TYPE_STRING, "http",
      "resource-port", G_TYPE_INT, port,
#else
      "resource-protocol", G_TYPE_STRING, "rtsp",
      "resource-port", G_TYPE_INT, port,
#endif
      "resource-path", G_TYPE_STRING, resource_path,
      "base-time", G_TYPE_INT64, (gint64) (manager->base_time),
      "position", G_TYPE_INT64, (gint64) (position),
      "paused", G_TYPE_BOOLEAN, manager->paused, NULL);

  g_free (resource_path);

  return msg;
}

static GstStructure *
manager_make_player_clients_changed_msg (G_GNUC_UNUSED SnraManager * manager)
{
  GstStructure *msg;
  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "player-clients-changed", NULL);
  return msg;
}
