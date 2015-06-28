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

#include <src/common/aur-json.h>

#include "aur-config.h"
#include "aur-http-resource.h"
#include "aur-manager.h"
#include "aur-media-db.h"
#include "aur-server.h"
#include "aur-server-client.h"

#ifdef HAVE_GST_RTSP
#include "aur-rtsp-media.h"
#endif

/* Set to 0 to walk the playlist linearly */
#define RANDOM_SHUFFLE 1

/* FIXME: Adjust client latency as needed */
#define CLIENT_LATENCY (GST_SECOND / 30)

enum
{
  PROP_0,
  PROP_CONFIG,
  PROP_LAST
};

typedef struct _AurPlayerInfo AurPlayerInfo;
struct _AurPlayerInfo
{
  guint id;
  gchar *host;
  AurServerClient *conn;

  gdouble volume;
  gboolean enabled;
};

typedef enum _AurControlEvent AurControlEvent;
enum _AurControlEvent
{
  AUR_CONTROL_NONE,
  AUR_CONTROL_NEXT,
  AUR_CONTROL_PREV,
  AUR_CONTROL_PLAY,
  AUR_CONTROL_PAUSE,
  AUR_CONTROL_ENQUEUE,
  AUR_CONTROL_VOLUME,
  AUR_CONTROL_CLIENT_SETTING,
  AUR_CONTROL_SEEK,
  AUR_CONTROL_LANGUAGE
};

static const struct
{
  const char *name;
  AurControlEvent type;
} control_event_names[] = {
  {
  "next", AUR_CONTROL_NEXT}, {
  "previous", AUR_CONTROL_PREV}, {
  "play", AUR_CONTROL_PLAY}, {
  "pause", AUR_CONTROL_PAUSE}, {
  "enqueue", AUR_CONTROL_ENQUEUE}, {
  "volume", AUR_CONTROL_VOLUME}, {
  "setclient", AUR_CONTROL_CLIENT_SETTING}, {
  "seek", AUR_CONTROL_SEEK}, {
  "language", AUR_CONTROL_LANGUAGE}
};

static const gint N_CONTROL_EVENTS = G_N_ELEMENTS (control_event_names);

GST_DEBUG_CATEGORY_STATIC (aur_manager_debug);
#define GST_CAT_DEFAULT aur_manager_debug

static void
init_debug ()
{
  GST_DEBUG_CATEGORY_INIT (aur_manager_debug, "aurena::manager", 0,
     "Aurena Manager object debug");
}

G_DEFINE_TYPE_WITH_CODE (AurManager, aur_manager, G_TYPE_OBJECT, init_debug() );

static void aur_manager_dispose (GObject * object);
static void aur_manager_finalize (GObject * object);
static AurHttpResource *aur_manager_get_resource_cb (AurServer * server,
    guint resource_id, void *userdata);
static void aur_manager_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void aur_manager_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void aur_manager_play_resource (AurManager * manager, guint resource_id);
static void aur_manager_send_play (AurManager * manager,
    AurServerClient * client);
static void aur_manager_send_pause (AurManager * manager,
    AurServerClient * client);
static void aur_manager_adjust_volume (AurManager * manager, gdouble volume);
static void aur_manager_adjust_client_volume (AurManager * manager,
    guint client_id, gdouble volume);
static void aur_manager_adjust_client_setting (AurManager * manager,
    guint client_id, gboolean enable);
static GstStructure *manager_make_set_media_msg (AurManager * manager,
    guint resource_id);
static GstStructure *manager_make_player_clients_changed_msg
    (AurManager * manager);
static AurPlayerInfo *get_player_info_by_id (AurManager * manager,
    guint client_id);
static void aur_manager_send_seek (AurManager * manager,
    AurServerClient * client, GstClockTime position);
static void aur_manager_send_language (AurManager * manager,
    AurServerClient * client, const gchar * language);

#define SEND_MSG_TO_PLAYERS 1
#define SEND_MSG_TO_DISABLED_PLAYERS 2
#define SEND_MSG_TO_ENABLED_PLAYERS 4
#define SEND_MSG_TO_CONTROLLERS 8
#define SEND_MSG_TO_ALL (SEND_MSG_TO_PLAYERS|SEND_MSG_TO_CONTROLLERS)

static void
manager_send_msg_to_client (AurManager * manager, AurServerClient * client,
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
create_rtsp_server (G_GNUC_UNUSED AurManager * mgr)
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
    GST_ERROR_OBJECT (mgr, "failed to attach the server");
    gst_object_unref (server);
    return NULL;
  }
}
#endif

static GstStructure *
manager_make_enrol_msg (AurManager * manager, AurPlayerInfo * info)
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

  if (info != NULL)             /* Is a player message */
    volume *= info->volume;

  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "enrol",
      "resource-id", G_TYPE_INT64, (gint64) manager->current_resource,
      "clock-port", G_TYPE_INT, clock_port,
      "current-time", G_TYPE_INT64, (gint64) (cur_time),
      "volume-level", G_TYPE_DOUBLE, volume,
      "paused", G_TYPE_BOOLEAN, manager->paused, NULL);

  if (info != NULL)             /* Is a player message */
    gst_structure_set (msg, "enabled", G_TYPE_BOOLEAN, info->enabled, NULL);

  return msg;
}

static GstStructure *
make_player_clients_list_msg (AurManager * manager)
{
  GstStructure *msg;
  GValue p = G_VALUE_INIT;
  GList *cur;

  g_value_init (&p, GST_TYPE_ARRAY);

  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "player-clients", NULL);

  for (cur = manager->player_info; cur != NULL; cur = g_list_next (cur)) {
    AurPlayerInfo *info = (AurPlayerInfo *) (cur->data);
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
find_player_info_by_client (const AurPlayerInfo * info,
    AurServerClient * client)
{
  if (info->conn == client)
    return 0;

  return -1;
}

static void
manager_player_client_disconnect (AurServerClient * client,
    AurManager * manager)
{
  GList *item = g_list_find_custom (manager->player_info, client, (GCompareFunc)(find_player_info_by_client));
  if (item) {
    AurPlayerInfo *info = (AurPlayerInfo *)(item->data);

    GST_INFO_OBJECT (manager, "Disconnecting player client %u", info->id);

    g_object_unref (client);
    info->conn = NULL;

    manager_send_msg_to_client (manager, NULL, SEND_MSG_TO_CONTROLLERS,
        manager_make_player_clients_changed_msg (manager));
  }
}

static void
manager_ctrl_client_disconnect (AurServerClient * client,
    AurManager * manager)
{
  GList *item = g_list_find (manager->ctrl_clients, client);
  if (item) {
    GST_INFO_OBJECT (manager, "Removing control client %u", client->conn_id);
    g_object_unref (client);
    manager->ctrl_clients = g_list_delete_link (manager->ctrl_clients, item);
  }
}

static void
manager_status_client_disconnect (AurServerClient * client,
    G_GNUC_UNUSED AurManager * manager)
{
  g_object_unref (client);
}

static gboolean
handle_ping_timeout (AurManager *manager)
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
send_enrol_events (AurManager * manager, AurServerClient * client,
    AurPlayerInfo *info)
{
  manager_send_msg_to_client (manager, client, SEND_MSG_TO_ALL,
      manager_make_enrol_msg (manager, info));

  if (manager->current_resource) {
    GST_DEBUG_OBJECT (manager, "Enrolling client on connection %u",
        client->conn_id);

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
find_unlinked_player_info_by_host (const AurPlayerInfo *p, const gchar *host)
{
  if (p->conn == NULL && g_str_equal (p->host, host))
    return 0;
  return -1;
}

static gint
find_player_info_by_client_id (const AurPlayerInfo *p, const gpointer cid_ptr)
{
  const guint client_id = GPOINTER_TO_INT(cid_ptr);
  if (p->id == client_id)
    return 0;
  return -1;
}

static AurPlayerInfo *
get_player_info_by_id (AurManager *manager, guint client_id)
{
  GList *entry;
  AurPlayerInfo *info = NULL;

  /* See if there's a player instance that matches */
  entry = g_list_find_custom (manager->player_info, GINT_TO_POINTER (client_id), (GCompareFunc) find_player_info_by_client_id);
  if (entry)
    info = (AurPlayerInfo *)(entry->data);

  return info;
}

static AurPlayerInfo *
get_player_info_for_client (AurManager *manager, AurServerClient *client)
{
  GList *entry;
  AurPlayerInfo *info = NULL;
  const gchar *host;

  /* See if there's a disconnected player instance that matches */
  host = aur_server_client_get_host (client);
  entry = g_list_find_custom (manager->player_info, host, (GCompareFunc) find_unlinked_player_info_by_host);
  if (entry == NULL) {
    info = g_new0 (AurPlayerInfo, 1);
    /* Init the player info */
    info->host = g_strdup (host);
    info->id = manager->next_player_id++;
    info->volume = 1.0;
    /* FIXME: Disable new clients if playing, otherwise enable */
    info->enabled = manager->paused;
    manager->player_info = entry = g_list_prepend (manager->player_info, info);

    GST_INFO_OBJECT (manager, "New player id %u", info->id);
  }
  else {
    info = (AurPlayerInfo *)(entry->data);
    GST_INFO_OBJECT (manager, "Player id %u rejoining", info->id);
  }

  info->conn = client;

  return info;
}

static void
manager_client_cb (SoupServer * soup, SoupMessage * msg,
    G_GNUC_UNUSED const char *path, G_GNUC_UNUSED GHashTable * query,
    G_GNUC_UNUSED SoupClientContext *ctx, AurManager * manager)
{
  AurServerClient *client_conn = NULL;
  gchar **parts = g_strsplit (path, "/", 3);
  guint n_parts = g_strv_length (parts);

  if (n_parts < 3 || !g_str_equal ("client", parts[1])) {
    soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
    goto done;                  /* Invalid request */
  }

  if (g_str_equal (parts[2], "player_events")) {
    AurPlayerInfo *info;

    client_conn = aur_server_client_new (soup, msg, ctx);
    g_signal_connect (client_conn, "connection-lost",
        G_CALLBACK (manager_player_client_disconnect), manager);

    info = get_player_info_for_client (manager, client_conn);

    send_enrol_events (manager, client_conn, info);
    manager_send_msg_to_client (manager, NULL, SEND_MSG_TO_CONTROLLERS,
        manager_make_player_clients_changed_msg (manager));
  } else if (g_str_equal (parts[2], "control_events")) {
    client_conn = aur_server_client_new (soup, msg, ctx);
    g_signal_connect (client_conn, "connection-lost",
        G_CALLBACK (manager_ctrl_client_disconnect), manager);
    manager->ctrl_clients = g_list_prepend (manager->ctrl_clients, client_conn);
    send_enrol_events (manager, client_conn, NULL);
  } else if (g_str_equal (parts[2], "player_info")) {
    client_conn = aur_server_client_new_single (soup, msg, ctx);
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
manager_send_msg_to_client (AurManager * manager, AurServerClient * client,
    gint send_to_mask, GstStructure * msg)
{
  JsonGenerator *gen;
  JsonNode *root;
  gchar *body;
  gsize len;

  root = aur_json_from_gst_structure (msg);
  gst_structure_free (msg);

  gen = json_generator_new ();

  json_generator_set_root (gen, root);

  body = json_generator_to_data (gen, &len);

  g_object_unref (gen);
  json_node_free (root);

  if (client) {
    aur_server_client_send_message (client, body, len);
  } else {
    /* client == NULL - send to all clients */
    GList *cur;
    if (send_to_mask & SEND_MSG_TO_PLAYERS) {
      for (cur = manager->player_info; cur != NULL; cur = g_list_next (cur)) {
        AurPlayerInfo *info = (AurPlayerInfo *) (cur->data);
        if (info->conn)
          aur_server_client_send_message (info->conn, body, len);
      }
    }
    if (send_to_mask & SEND_MSG_TO_CONTROLLERS) {
      for (cur = manager->ctrl_clients; cur != NULL; cur = g_list_next (cur)) {
        client = (AurServerClient *) (cur->data);
        aur_server_client_send_message (client, body, len);
      }
    }
  }
  g_free (body);
}

static AurControlEvent
str_to_control_event_type (const gchar * str)
{
  gint i;
  for (i = 0; i < N_CONTROL_EVENTS; i++) {
    if (g_str_equal (str, control_event_names[i].name))
      return control_event_names[i].type;
  }

  return AUR_CONTROL_NONE;
}

static guint
get_playlist_len (AurManager * mgr)
{
  return aur_media_db_get_file_count (mgr->media_db);
}

static const gchar *
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

static gboolean
is_allowed_uri (const gchar *uri)
{
  /* Allow only http requests for now */
  if (g_str_has_prefix (uri, "http://"))
    return TRUE;

  return FALSE;
}

static void
control_callback (G_GNUC_UNUSED SoupServer * soup, SoupMessage * msg,
    const char *path, GHashTable * query,
    G_GNUC_UNUSED SoupClientContext * client, AurManager * manager)
{
  gchar **parts = g_strsplit (path, "/", 3);
  guint n_parts = g_strv_length (parts);
  AurControlEvent event_type;
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
    case AUR_CONTROL_NEXT:{
      const gchar *id_str = find_param_str ("id", query, post_params);
      guint resource_id;

      GST_INFO_OBJECT (manager, "Next track to play is %s\n", id_str);

      /* Accept two forms of ID. If the ID can be parsed as an integer, use it
       * to look up a file in the media DB and enqueue that. Otherwise, treat
       * the ID as a URI and use the manager's custom_file functionality to
       * enqueue that URI (which probably isn't in the media DB). Custom files
       * are signified by the resource ID G_MAXUINT. Note that URIs may be
       * fully qualified URIs, or absolute paths on the server (beginning with
       * '/').
       */
      if (id_str && !g_ascii_isdigit (id_str[0])) {
        if (!is_allowed_uri (id_str))
          break;
        resource_id = G_MAXUINT;
        g_clear_object (&manager->custom_file);
        manager->custom_file = g_file_new_for_commandline_arg (id_str);
      } else if (get_playlist_len (manager) == 0) {
        resource_id = 0;
      } else if (id_str == NULL || !id_str[0]
          || !sscanf (id_str, "%d", &resource_id)) {
        /* No or invalid resource id: skip to next track */
#if RANDOM_SHUFFLE
        resource_id =
          (guint) g_random_int_range (0, get_playlist_len (manager)) + 1;
#else
        resource_id =
          (guint) (manager->current_resource % get_playlist_len (manager)) + 1;
#endif
      } else {
        resource_id = CLAMP (resource_id, 1, get_playlist_len (manager));
      }
      manager->paused = FALSE;
      aur_manager_play_resource (manager, resource_id);
      break;
    }
    case AUR_CONTROL_PAUSE:{
      if (!manager->paused)
        aur_manager_send_pause (manager, NULL);
      manager->paused = TRUE;
      break;
    }
    case AUR_CONTROL_PLAY:{
      if (manager->paused) {
        if (manager->current_resource == 0) {
          guint resource_id =
              g_random_int_range (0, get_playlist_len (manager) + 1);
          if (resource_id != 0) {
            manager->paused = FALSE;
            aur_manager_play_resource (manager, resource_id);
          }
        } else {
          manager->paused = FALSE;
          aur_manager_send_play (manager, NULL);
        }
      }
      break;
    }
    case AUR_CONTROL_VOLUME:{
      const gchar *vol_str = find_param_str ("level", query, post_params);
      const gchar *id_str = find_param_str ("client_id", query, post_params);
      guint client_id = 0;
      gdouble new_vol;

      if (id_str != NULL)
        sscanf (id_str, "%u", &client_id);

      if (vol_str) {
        new_vol = g_ascii_strtod (vol_str, NULL);
        new_vol = CLAMP (new_vol, 0.0, 10.0);
        if (client_id == 0)
          aur_manager_adjust_volume (manager, new_vol);
        else
          aur_manager_adjust_client_volume (manager, client_id, new_vol);
      }

      break;
    }
    case AUR_CONTROL_CLIENT_SETTING:{
      const gchar *set_str = find_param_str ("enable", query, post_params);
      const gchar *id_str = find_param_str ("client_id", query, post_params);
      guint client_id = 0;
      gint enable = 1;

      if (id_str != NULL)
        sscanf (id_str, "%u", &client_id);

      if (set_str && sscanf (set_str, "%d", &enable)) {
        if (client_id > 0)
          aur_manager_adjust_client_setting (manager, client_id, enable != 0);
      }

      break;
    }
    case AUR_CONTROL_SEEK:{
      const gchar *pos_str = find_param_str ("position", query, post_params);
      GstClockTime position = 0;

      if (pos_str != NULL)
        sscanf (pos_str, "%" G_GUINT64_FORMAT, &position);

      aur_manager_send_seek (manager, NULL, position);
      break;
    }
    case AUR_CONTROL_LANGUAGE:{
      const gchar *language = find_param_str ("language", query, post_params);
      aur_manager_send_language (manager, NULL, language);
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
aur_manager_init (AurManager * manager)
{
  manager->playlist = g_ptr_array_new ();
  manager->net_clock = create_net_clock ();
  manager->paused = TRUE;
  manager->language = g_strdup ("en");

  manager->base_time = GST_CLOCK_TIME_NONE;
  manager->position = 0;
  manager->current_volume = 0.1;

  manager->current_resource = 0;
  manager->next_player_id = 1;
}

static void
aur_manager_constructed (GObject * object)
{
  AurManager *manager = (AurManager *) (object);
  gchar *db_file;
  int aur_port;

  if (G_OBJECT_CLASS (aur_manager_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (aur_manager_parent_class)->constructed (object);

#ifdef HAVE_GST_RTSP
  manager->rtsp = create_rtsp_server (manager);
#endif

  g_object_get (manager->config, "aur-port", &aur_port, NULL);

  manager->server = g_object_new (AUR_TYPE_SERVER,
      "config", manager->config, NULL);
  aur_server_set_resource_cb (manager->server,
      aur_manager_get_resource_cb, manager);
  aur_server_add_handler (manager->server, "/control",
      (SoupServerCallback) control_callback,
      g_object_ref (G_OBJECT (manager)), g_object_unref);
  aur_server_add_handler (manager->server, "/client",
      (SoupServerCallback) manager_client_cb,
      g_object_ref (manager), g_object_unref);

  manager->avahi = g_object_new (AUR_TYPE_AVAHI, "aur-port", aur_port, NULL);

  g_object_get (manager->config, "database", &db_file, NULL);
  manager->media_db = aur_media_db_new (db_file);
  g_free (db_file);
}

static void
aur_manager_class_init (AurManagerClass * manager_class)
{
  GObjectClass *object_class = (GObjectClass *) (manager_class);

  object_class->constructed = aur_manager_constructed;
  object_class->set_property = aur_manager_set_property;
  object_class->get_property = aur_manager_get_property;
  object_class->dispose = aur_manager_dispose;
  object_class->finalize = aur_manager_finalize;

  g_object_class_install_property (object_class, PROP_CONFIG,
      g_param_spec_object ("config", "config",
          "Aurena service configuration object",
          AUR_TYPE_CONFIG, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
free_player_info (AurPlayerInfo *info)
{
  if (info->conn)
    g_object_unref (info->conn);
  g_free (info->host);
  g_free (info);
}

static void
aur_manager_dispose (GObject * object)
{
  AurManager *manager = (AurManager *) (object);

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

  G_OBJECT_CLASS (aur_manager_parent_class)->dispose (object);
}

static void
aur_manager_finalize (GObject * object)
{
  AurManager *manager = (AurManager *) (object);

  g_ptr_array_foreach (manager->playlist, (GFunc) g_free, NULL);
  g_ptr_array_free (manager->playlist, TRUE);

  g_object_unref (manager->config);
  g_object_unref (manager->media_db);

  aur_server_stop (manager->server);
  g_object_unref (manager->server);

  g_clear_object (&manager->custom_file);
  g_free (manager->language);
}

static void
aur_manager_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  AurManager *manager = (AurManager *) (object);

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
aur_manager_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  AurManager *manager = (AurManager *) (object);

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
add_rtsp_uri (AurManager * manager, guint resource_id,
    const gchar * source_uri)
{
  GstRTSPMountPoints *mount;
  GstRTSPMediaFactoryURI *factory;
  gchar *rtsp_uri = g_strdup_printf ("/resource/%u", resource_id);
  GstClock *clock;

  mount = gst_rtsp_server_get_mount_points (manager->rtsp);
  g_object_get (manager->net_clock, "clock", &clock, NULL);
  factory = aur_rtsp_media_factory_uri_new (clock);

  GST_DEBUG_OBJECT (manager, "Registering RTSP path %s for %s",
     rtsp_uri, source_uri);

  /* Set up the URI, and set as shared (all viewers see the same stream) */
  gst_rtsp_media_factory_uri_set_uri (factory, source_uri);
  gst_rtsp_media_factory_set_shared (GST_RTSP_MEDIA_FACTORY (factory), TRUE);
  gst_rtsp_media_factory_set_media_gtype (GST_RTSP_MEDIA_FACTORY (factory),
      AUR_TYPE_RTSP_MEDIA);
  /* attach the test factory to the test url */
  gst_rtsp_mount_points_add_factory (mount, rtsp_uri,
      GST_RTSP_MEDIA_FACTORY (factory));
  g_object_unref (mount);

  g_free (rtsp_uri);
}
#endif

static void
read_playlist_file (AurManager * manager, const char *filename)
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

  aur_media_db_begin_transaction (manager->media_db);

  do {
    GFile *file;

    result = g_io_channel_read_line (io, &line, NULL, NULL, NULL);
    if (result == G_IO_STATUS_AGAIN)
      continue;
    if (result != G_IO_STATUS_NORMAL)
      break;
    g_strchomp (line);
    g_ptr_array_add (manager->playlist, line);

    /* The line could either be a URI or an absolute path, for new- and
     * old-style playlist files, respectively. g_file_new_for_commandline_arg()
     * doesn't care. */
    file = g_file_new_for_commandline_arg (line);
    aur_media_db_add_file (manager->media_db, file);
    g_object_unref (file);
  } while (TRUE);
  aur_media_db_commit_transaction (manager->media_db);

  g_print ("Finished scanning playlist. Read %u entries\n", manager->playlist->len);

  g_io_channel_unref (io);
}


AurManager *
aur_manager_new (const char *config_file)
{
  AurConfig *config;
  AurManager *manager;
  gchar *playlist_file;
  gint i;

  config = aur_config_new (config_file);
  if (config == NULL)
    return NULL;

  manager = g_object_new (AUR_TYPE_MANAGER, "config", config, NULL);

  g_object_get (config, "playlist", &playlist_file, NULL);
  if (playlist_file) {
    read_playlist_file (manager, playlist_file);
    g_free (playlist_file);
  }

#ifdef HAVE_GST_RTSP
  for (i = get_playlist_len (manager); i > 0; i--) {
    char *rtsp_uri = g_strdup_printf ("file://%s",
        (gchar *) (g_ptr_array_index (manager->playlist, i-1)));
    add_rtsp_uri (manager, i, rtsp_uri);
    g_free (rtsp_uri);
  }
#endif

  aur_server_start (manager->server);

  return manager;
}

static AurHttpResource *
aur_manager_get_resource_cb (G_GNUC_UNUSED AurServer * server,
    guint resource_id, void *userdata)
{
  AurManager *manager = (AurManager *) (userdata);
  AurHttpResource *ret;
  GFile *file;
  gchar *file_uri;

  if (resource_id == G_MAXUINT && manager->custom_file)
    return g_object_new (AUR_TYPE_HTTP_RESOURCE, "source-file",
        manager->custom_file, NULL);

  if (resource_id < 1 || resource_id > get_playlist_len (manager))
    return NULL;

  file = aur_media_db_get_file_by_id (manager->media_db, resource_id);
  if (file == NULL)
    return NULL;

  file_uri = g_file_get_uri (file);
  GST_DEBUG_OBJECT (manager, "Creating resource %u for %s", resource_id, file_uri);
  g_free (file_uri);

  ret = g_object_new (AUR_TYPE_HTTP_RESOURCE, "source-file", file, NULL);
  g_object_unref (file);

  return ret;
}

static void
aur_manager_play_resource (AurManager * manager, guint resource_id)
{
  manager->current_resource = resource_id;
  manager->base_time = GST_CLOCK_TIME_NONE;
  manager->position = 0;

  GST_DEBUG_OBJECT (manager, "Playing resource %u", resource_id);

  manager_send_msg_to_client (manager, NULL, SEND_MSG_TO_ALL,
      manager_make_set_media_msg (manager, manager->current_resource));
}

static void
aur_manager_send_play (AurManager * manager, AurServerClient * client)
{
  GstClock *clock;
  GstStructure *msg;

  /* Update base time to match length of time paused */
  g_object_get (manager->net_clock, "clock", &clock, NULL);
  manager->base_time = gst_clock_get_time (clock) - manager->position + CLIENT_LATENCY;
  gst_object_unref (clock);
  manager->position = 0;

  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "play",
      "latency", G_TYPE_INT64, (gint64)(CLIENT_LATENCY),
      "base-time", G_TYPE_INT64, (gint64) (manager->base_time), NULL);

  manager_send_msg_to_client (manager, client, SEND_MSG_TO_ALL, msg);
}

static void
aur_manager_send_pause (AurManager * manager, AurServerClient * client)
{
  GstClock *clock;
  GstClockTime now;
  GstStructure *msg;

  g_object_get (manager->net_clock, "clock", &clock, NULL);
  now = gst_clock_get_time (clock);
  gst_object_unref (clock);

  /* Calculate how much of the current file we played up until now, and store */
  manager->position = now - manager->base_time + (GST_SECOND / 30);
  GST_INFO_OBJECT (manager, "Pausing - storing position %" GST_TIME_FORMAT,
      GST_TIME_ARGS (manager->position));

  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "pause",
      "position", G_TYPE_INT64, (gint64) manager->position,
      NULL);

  manager_send_msg_to_client (manager, client, SEND_MSG_TO_ALL, msg);
}

static void
aur_manager_adjust_client_volume (AurManager * manager, guint client_id,
    gdouble volume)
{
  GstStructure *msg = NULL;
  AurPlayerInfo *info;

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
aur_manager_adjust_client_setting (AurManager * manager, guint client_id,
    gboolean enable)
{
  GstStructure *msg = NULL;
  AurPlayerInfo *info;

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
aur_manager_adjust_volume (AurManager * manager, gdouble volume)
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
    AurPlayerInfo *info = (AurPlayerInfo *)(cur->data);
    msg = gst_structure_new ("json",
           "msg-type", G_TYPE_STRING, "volume",
           "level", G_TYPE_DOUBLE, info->volume * volume, NULL);
    manager_send_msg_to_client (manager, info->conn, 0, msg);
  }
}

static void
aur_manager_send_seek (AurManager * manager, AurServerClient * client,
    GstClockTime position)
{
  GstClock *clock;
  GstClockTime now;
  GstStructure *msg;

  g_object_get (manager->net_clock, "clock", &clock, NULL);
  now = gst_clock_get_time (clock);
  gst_object_unref (clock);

  manager->base_time = now - position + (GST_SECOND / 4);
  if (manager->paused)
    manager->position = position;

  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "seek",
      "base-time", G_TYPE_INT64, (gint64) manager->base_time,
      "position", G_TYPE_INT64, (gint64) position,
      NULL);

  manager_send_msg_to_client (manager, client, SEND_MSG_TO_ALL, msg);
}

static void
aur_manager_send_language (AurManager * manager, AurServerClient * client,
    const gchar * language)
{
  GstStructure *msg;

  g_free (manager->language);
  manager->language = g_strdup (language ? language : "en");

  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "language",
      "language", G_TYPE_STRING, manager->language,
      NULL);

  manager_send_msg_to_client (manager, client, SEND_MSG_TO_ALL, msg);
}

static GstStructure *
manager_make_set_media_msg (AurManager * manager, guint resource_id)
{
  GstClock *clock;
  GstClockTime cur_time, position;
  gchar *resource_path;
  GstStructure *msg;
  gint port;
#if HAVE_GST_RTSP
#endif

  g_object_get (manager->net_clock, "clock", &clock, NULL);
  cur_time = gst_clock_get_time (clock);
  gst_object_unref (clock);

  resource_path = g_strdup_printf ("/resource/%u", resource_id);

  if (manager->base_time == GST_CLOCK_TIME_NONE) {
    // configure a base time 0.25 seconds in the future
    manager->base_time = cur_time + (GST_SECOND / 4);
    manager->position = 0;
    GST_LOG_OBJECT (manager, "Base time now %" G_GUINT64_FORMAT, manager->base_time);
  }
#if HAVE_GST_RTSP
  g_object_get (manager->config, "rtsp-port", &port, NULL);
#else
  g_object_get (manager->config, "aur-port", &port, NULL);
#endif

  /* Calculate position if currently playing */
  if (!manager->paused && cur_time > manager->base_time)
    position = cur_time - manager->base_time;
  else
    position = manager->position;

  msg = gst_structure_new ("json", "msg-type", G_TYPE_STRING, "set-media",
      "resource-id", G_TYPE_INT64, (gint64) resource_id,
#if HAVE_GST_RTSP
      "resource-protocol", G_TYPE_STRING, "rtsp",
      "resource-port", G_TYPE_INT, port,
#else
      "resource-protocol", G_TYPE_STRING, "http",
      "resource-port", G_TYPE_INT, port,
#endif
      "resource-path", G_TYPE_STRING, resource_path,
      "base-time", G_TYPE_INT64, (gint64) (manager->base_time),
      "latency", G_TYPE_INT64, (gint64)(CLIENT_LATENCY),
      "position", G_TYPE_INT64, (gint64) (position),
      "paused", G_TYPE_BOOLEAN, manager->paused,
      "language", G_TYPE_STRING, manager->language, NULL);

  g_free (resource_path);

  return msg;
}

static GstStructure *
manager_make_player_clients_changed_msg (G_GNUC_UNUSED AurManager * manager)
{
  GstStructure *msg;
  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "player-clients-changed", NULL);
  return msg;
}
