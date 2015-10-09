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

#include <string.h>

#include <json-glib/json-glib.h>

#include <common/aur-json.h>
#include <common/aur-config.h>
#include <common/aur-event.h>
#include <common/aur-component.h>

#include "aur-http-resource.h"
#include "aur-manager.h"
#include "aur-media-db.h"
#include "aur-receiver.h"
#include "aur-rtsp-play-media.h"
#include "aur-server.h"
#include "aur-http-client.h"
#include "aur-client-proxy.h"

GST_DEBUG_CATEGORY_STATIC (manager_debug);
#define GST_CAT_DEFAULT manager_debug

/* Set to 0 to walk the playlist linearly */
#define RANDOM_SHUFFLE 1

enum
{
  PROP_0,
  PROP_CONFIG,
  PROP_LAST
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

static void
_do_init ()
{
  GST_DEBUG_CATEGORY_INIT (manager_debug, "aurena/manager", 0,
      "Aurena Manager debug");
}

G_DEFINE_TYPE_WITH_CODE (AurManager, aur_manager, G_TYPE_OBJECT, _do_init ());

static void aur_manager_dispose (GObject * object);
static void aur_manager_finalize (GObject * object);
static AurHttpResource *aur_manager_get_resource_cb (AurServer * server,
    guint resource_id, void *userdata);
static void aur_manager_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void aur_manager_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void aur_manager_play_resource (AurManager * manager, guint resource_id);
static void aur_manager_send_play (AurManager * manager);
static void aur_manager_send_pause (AurManager * manager);
static void aur_manager_adjust_volume (AurManager * manager, gdouble volume);
static void aur_manager_adjust_client_volume (AurManager * manager,
    guint client_id, gdouble volume);
static void aur_manager_adjust_client_setting (AurManager * manager,
    guint client_id, gboolean enable, gboolean record_enable);
static AurEvent *manager_make_set_media_event (AurManager * manager,
    guint resource_id);
static AurEvent *manager_make_player_clients_changed_event
    (AurManager * manager);
static AurClientProxy *get_client_proxy_by_id (AurManager * manager,
    guint client_id, AurComponentRole roles);
static void aur_manager_send_seek (AurManager * manager, GstClockTime position);
static void aur_manager_send_language (AurManager * manager,
    const gchar * language);
static AurEvent *manager_make_record_event (AurManager * manager,
    AurClientProxy * proxy);

static void
manager_send_event_to_connection (AurManager * manager, AurHTTPClient * conn,
    AurComponentRole targets, AurEvent * event);
static void manager_send_event_to_client (AurManager * manager,
    AurClientProxy * client, AurComponentRole targets, AurEvent * event);
static void manager_send_event_to_clients (AurManager * manager,
    AurComponentRole targets, AurEvent * event);

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

static GstRTSPServer *
create_rtsp_server (AurManager * mgr)
{
  GstRTSPServer *server = NULL;
  gchar *rtsp_service;

  server = gst_rtsp_server_new ();

  GST_DEBUG_OBJECT (mgr, "Creating RTSP server on port %d", mgr->rtsp_port);

  rtsp_service = g_strdup_printf ("%u", mgr->rtsp_port);
  gst_rtsp_server_set_service (server, rtsp_service);
  g_free (rtsp_service);

  /* attach the server to the default maincontext */
  if (gst_rtsp_server_attach (server, NULL) == 0)
    goto failed;

  /* start serving */
  return server;

  /* ERRORS */
failed:
  {
    g_print ("failed to attach the RTSP service port\n");
    gst_object_unref (server);
    return NULL;
  }
}

static AurEvent *
manager_make_enrol_event (AurManager * manager, AurClientProxy * proxy)
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

  if (proxy != NULL)            /* Is a player message */
    volume *= proxy->volume;

  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "enrol",
      "client-id", G_TYPE_INT64, (gint64) proxy->id,
      "resource-id", G_TYPE_INT64, (gint64) manager->current_resource,
      "clock-port", G_TYPE_INT, clock_port,
      "current-time", G_TYPE_INT64, (gint64) (cur_time),
      "volume-level", G_TYPE_DOUBLE, volume,
      "paused", G_TYPE_BOOLEAN, manager->paused, NULL);

  if (proxy != NULL)            /* Is a player message */
    gst_structure_set (msg, "enabled", G_TYPE_BOOLEAN, proxy->enabled, NULL);

  return aur_event_new (msg);
}

static AurEvent *
make_player_clients_list_event (AurManager * manager)
{
  GstStructure *msg;
  GValue p = G_VALUE_INIT;
  GList *cur;

  g_value_init (&p, GST_TYPE_ARRAY);

  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "player-clients", NULL);

  for (cur = manager->clients; cur != NULL; cur = g_list_next (cur)) {
    AurClientProxy *proxy = AUR_CLIENT_PROXY (cur->data);

    if ((proxy->roles & AUR_COMPONENT_ROLE_PLAYER) == 0)
      continue;

    if (proxy->conn != NULL) {
      GValue tmp = G_VALUE_INIT;
      GstStructure *cur_struct = gst_structure_new ("client",
          "client-id", G_TYPE_INT64, (gint64) proxy->id,
          "enabled", G_TYPE_BOOLEAN, proxy->enabled,
          "record-enabled", G_TYPE_BOOLEAN, proxy->record_enabled,
          "volume", G_TYPE_DOUBLE, proxy->volume,
          "host", G_TYPE_STRING, proxy->name,
          NULL);

      g_value_init (&tmp, GST_TYPE_STRUCTURE);
      gst_value_set_structure (&tmp, cur_struct);
      gst_value_array_append_value (&p, &tmp);
      g_value_unset (&tmp);
      gst_structure_free (cur_struct);
    }
  }
  gst_structure_take_value (msg, "player-clients", &p);

  return aur_event_new (msg);
}

static gint
find_client_proxy_by_client (const AurClientProxy * proxy,
    AurHTTPClient * client)
{
  if (proxy->conn == client)
    return 0;

  return -1;
}

static void
client_disconnect (AurHTTPClient * client, AurManager * manager)
{
  GList *item = g_list_find_custom (manager->clients, client,
      (GCompareFunc) (find_client_proxy_by_client));
  if (item) {
    AurClientProxy *proxy = (AurClientProxy *) (item->data);

    g_print ("Disconnecting client %u (roles 0x%x)\n", proxy->id, proxy->roles);

    g_object_unref (client);
    proxy->conn = NULL;

    if (proxy->roles & AUR_COMPONENT_ROLE_PLAYER) {
      manager_send_event_to_clients (manager, AUR_COMPONENT_ROLE_CONTROLLER,
          manager_make_player_clients_changed_event (manager));
    }
  }
}

static void
manager_status_client_disconnect (AurHTTPClient * client,
    G_GNUC_UNUSED AurManager * manager)
{
  g_object_unref (client);
}

static gboolean
handle_ping_timeout (AurManager * manager)
{
  AurEvent *event;

  if (manager->ping_timeout == 0)
    return FALSE;

  /* Send a ping to each client */
  event = aur_event_new (gst_structure_new ("json",
          "msg-type", G_TYPE_STRING, "ping", NULL));

  manager_send_event_to_clients (manager, AUR_COMPONENT_ROLE_ALL, event);

  return TRUE;
}

static void
send_enrol_events (AurManager * manager, AurClientProxy * proxy)
{
  manager_send_event_to_client (manager, proxy,
      AUR_COMPONENT_ROLE_ALL, manager_make_enrol_event (manager, proxy));

  if (manager->current_resource) {
    manager_send_event_to_client (manager, proxy, AUR_COMPONENT_ROLE_ALL,
        manager_make_set_media_event (manager, manager->current_resource));
  }

  if (proxy->roles & AUR_COMPONENT_ROLE_CONTROLLER) {
    manager_send_event_to_client (manager, proxy,
        AUR_COMPONENT_ROLE_CONTROLLER,
        manager_make_player_clients_changed_event (manager));
  }

  if (manager->ping_timeout == 0) {
    manager->ping_timeout =
        g_timeout_add_seconds (2, (GSourceFunc) handle_ping_timeout, manager);
  }
}

struct AurClientProxyFindClosure
{
  AurComponentRole roles;
  guint client_id;
  const gchar *host;
};

/* Match roles exactly for unlinked client search ... */
static gint
find_unlinked_client_by_host (const AurClientProxy * p,
    struct AurClientProxyFindClosure *c)
{
  if (p->conn == NULL && c->roles == p->roles && g_str_equal (p->host, c->host))
    return 0;
  return -1;
}

static gint
find_client_proxy_by_client_id (const AurClientProxy * p,
    struct AurClientProxyFindClosure *c)
{
  if (p->id == c->client_id && (p->roles & c->roles))
    return 0;
  return -1;
}

static AurClientProxy *
get_client_proxy_by_id (AurManager * manager,
    guint client_id, AurComponentRole roles)
{
  GList *entry;
  AurClientProxy *proxy = NULL;
  struct AurClientProxyFindClosure c;

  /* See if there's a player instance that matches */
  c.roles = roles;
  c.client_id = client_id;

  entry =
      g_list_find_custom (manager->clients, &c,
      (GCompareFunc) find_client_proxy_by_client_id);
  if (entry)
    proxy = (AurClientProxy *) (entry->data);

  return proxy;
}

static gchar *
lookup_static_host_to_name (const gchar *host)
{
  static const struct {
    const gchar *host;
    const gchar *name;
  } names[] = {
    { "192.168.1.245", "Nexus 7 2013" },
    { "192.168.1.238", "Jan Laptop" },
    { "192.168.1.144", "Galaxy S3" },
    { "192.168.1.115", "Nexus 7 2012" }
  };
  guint i;

  for (i=0; i < G_N_ELEMENTS (names); i++) {
    if (g_str_equal (names[i].host, host))
      return g_strdup (names[i].name);
  }

  return g_strdup (host);
}

static AurClientProxy *
get_client_proxy_for_client (AurManager * manager, AurHTTPClient * client,
    AurComponentRole roles)
{
  GList *entry;
  AurClientProxy *proxy = NULL;
  const gchar *host;
  struct AurClientProxyFindClosure c;

  /* See if there's a disconnected player instance that matches */
  host = aur_http_client_get_host (client);
  c.host = host;
  c.roles = roles;
  entry =
      g_list_find_custom (manager->clients, &c,
      (GCompareFunc) find_unlinked_client_by_host);
  if (entry == NULL) {
    proxy = g_object_new (AUR_TYPE_CLIENT_PROXY, NULL);
    /* Init the client proxy */
    proxy->roles = roles;
    proxy->host = g_strdup (host);
    proxy->name = lookup_static_host_to_name (host);
    proxy->id = manager->next_player_id++;
    proxy->volume = 1.0;
    /* FIXME: Disable new clients if playing, otherwise enable */
    proxy->enabled = manager->paused;
    manager->clients = entry = g_list_prepend (manager->clients, proxy);

    g_print ("New client id %u (roles %x)\n", proxy->id, proxy->roles);
  } else {
    proxy = (AurClientProxy *) (entry->data);
    g_print ("Client id %u rejoining (roles %x)\n", proxy->id, proxy->roles);
  }

  proxy->conn = client;

  return proxy;
}

static AurComponentRole
role_str_to_roles (const gchar * roles_str)
{
  AurComponentRole roles = 0;

  if (strstr (roles_str, "player"))
    roles |= AUR_COMPONENT_ROLE_PLAYER;
  if (strstr (roles_str, "controller"))
    roles |= AUR_COMPONENT_ROLE_CONTROLLER;
  if (strstr (roles_str, "capture"))
    roles |= AUR_COMPONENT_ROLE_CAPTURE;

  return roles;
}

static void
enrol_events_subscriber (AurManager * manager, SoupServer * soup,
    SoupMessage * msg, SoupClientContext * ctx, AurComponentRole roles)
{
  AurHTTPClient *client_conn = NULL;
  AurClientProxy *proxy;
  client_conn = aur_http_client_new (soup, msg, ctx);
  g_signal_connect (client_conn, "connection-lost",
      G_CALLBACK (client_disconnect), manager);
  proxy = get_client_proxy_for_client (manager, client_conn, roles);
  send_enrol_events (manager, proxy);

  if (proxy->roles & AUR_COMPONENT_ROLE_PLAYER) {
    manager_send_event_to_clients (manager,
        AUR_COMPONENT_ROLE_CONTROLLER,
        manager_make_player_clients_changed_event (manager));
  }
}

static void
handle_client_event (AurManager * manager, SoupMessage * msg)
{
  SoupBuffer *buffer;
  JsonNode *root;
  GstStructure *s;
  const gchar *msg_type;

  buffer = soup_message_body_flatten (msg->request_body);

  if (!buffer || !json_parser_load_from_data (manager->json,
          buffer->data, buffer->length, NULL) ||
      (root = json_parser_get_root (manager->json)) == NULL) {
    GST_WARNING_OBJECT (manager, "Invalid client event received");
    if (buffer != NULL)
      soup_buffer_free (buffer);
    return;
  }

  soup_buffer_free (buffer);

  s = aur_json_to_gst_structure (root);
  msg_type = gst_structure_get_string (s, "msg-type");
  if (msg_type == NULL) {
    GST_WARNING_OBJECT (manager, "Invalid client event received");
    return;
  }

  if (g_str_equal (msg_type, "client-stats")) {
    /* Forward the message directly to controllers */
    manager_send_event_to_clients (manager,
        AUR_COMPONENT_ROLE_CONTROLLER, aur_event_new (gst_structure_copy (s)));
  } else {
    g_print ("Unknown event of type %s\n", msg_type);
  }

  gst_structure_free (s);
}

static void
manager_client_cb (SoupServer * soup, SoupMessage * msg,
    const char *path, GHashTable * query,
    SoupClientContext * ctx, AurManager * manager)
{
  AurHTTPClient *client_conn = NULL;
  gchar **parts = g_strsplit (path, "/", 3);
  guint n_parts = g_strv_length (parts);

  if (n_parts < 3 || !g_str_equal ("client", parts[1])) {
    soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
    goto done;                  /* Invalid request */
  }

  if (g_str_equal (parts[2], "events")) {
    const gchar *roles_str;
    AurComponentRole roles;

    if (g_str_equal (msg->method, "POST")) {
      handle_client_event (manager, msg);
      goto done;
    }

    if (query == NULL ||
        ((roles_str = g_hash_table_lookup (query, "roles")) == NULL) ||
        ((roles = role_str_to_roles (roles_str)) == 0)) {
      soup_message_set_status (msg, SOUP_STATUS_BAD_REQUEST);
      goto done;                /* Invalid request */
    }
    enrol_events_subscriber (manager, soup, msg, ctx, roles);
#if 1                           /* Backwards compat */
  } else if (g_str_equal (parts[2], "player_events")) {
    enrol_events_subscriber (manager, soup, msg, ctx,
        AUR_COMPONENT_ROLE_PLAYER);
  } else if (g_str_equal (parts[2], "control_events")) {
    enrol_events_subscriber (manager, soup, msg, ctx,
        AUR_COMPONENT_ROLE_CONTROLLER);
#endif
  } else if (g_str_equal (parts[2], "player_info")) {
    client_conn = aur_http_client_new_single (soup, msg, ctx);
    g_signal_connect (client_conn, "connection-lost",
        G_CALLBACK (manager_status_client_disconnect), manager);
    manager_send_event_to_connection (manager,
        client_conn, AUR_COMPONENT_ROLE_CONTROLLER,
        make_player_clients_list_event (manager));
  } else {
    soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
  }

done:
  g_strfreev (parts);
}

static void
manager_send_event_to_connection (AurManager * manager, AurHTTPClient * client,
    AurComponentRole targets, AurEvent * event)
{
  gchar *body;
  gsize len;

  g_return_if_fail (event != NULL);

  body = aur_event_to_data (event, targets, &len);

  GST_LOG_OBJECT (manager, "Sending %s event to connection %d",
      aur_event_get_name (event), client->conn_id);
  aur_http_client_send_message (client, body, len);

  g_object_unref (event);
  g_free (body);
}

static void
manager_send_event_to_client (AurManager * manager, AurClientProxy * client,
    AurComponentRole targets, AurEvent * event)
{
  gchar *body;
  gsize len;

  g_return_if_fail (event != NULL);

  if (client->conn == NULL) {
    GST_LOG_OBJECT (manager, "Client %d not currently connected", client->id);
    g_object_unref (event);
    return;
  }

  body = aur_event_to_data (event, targets, &len);

  GST_LOG_OBJECT (manager, "Sending %s event to client %d",
      aur_event_get_name (event), client->id);
  aur_http_client_send_message (client->conn, body, len);

  g_object_unref (event);
  g_free (body);
}

static void
manager_send_event_to_clients (AurManager * manager,
    AurComponentRole targets, AurEvent * event)
{
  gchar *body;
  gsize len;
  GList *cur;

  g_return_if_fail (event != NULL);

  body = aur_event_to_data (event, targets, &len);

  /* Send to all matching clients */
  GST_LOG_OBJECT (manager, "Sending %s event to clients of type %x",
      aur_event_get_name (event), targets);

  for (cur = manager->clients; cur != NULL; cur = g_list_next (cur)) {
    AurClientProxy *proxy = (AurClientProxy *) (cur->data);
    if (proxy->conn && (targets & proxy->roles))
      aur_http_client_send_message (proxy->conn, body, len);
  }

  g_object_unref (event);
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
is_allowed_uri (const gchar * uri)
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

      g_print ("Next ID %s\n", id_str);

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
            (guint) (manager->current_resource % get_playlist_len (manager)) +
            1;
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
        aur_manager_send_pause (manager);
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
          aur_manager_send_play (manager);
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
      const gchar *record_set_str =
          find_param_str ("record_enable", query, post_params);
      const gchar *id_str = find_param_str ("client_id", query, post_params);
      guint client_id = 0;
      gint enable = 1, record_enable = 0;

      if (id_str == NULL || !sscanf (id_str, "%u", &client_id))
        break;
      if (client_id < 1)
        break;

      if (set_str == NULL || !sscanf (set_str, "%d", &enable))
        break;
      if (record_set_str == NULL
          || !sscanf (record_set_str, "%d", &record_enable))
        break;

      aur_manager_adjust_client_setting (manager, client_id, ! !enable,
          ! !record_enable);
      break;
    }
    case AUR_CONTROL_SEEK:{
      const gchar *pos_str = find_param_str ("position", query, post_params);
      GstClockTime position = 0;

      if (pos_str != NULL)
        sscanf (pos_str, "%" G_GUINT64_FORMAT, &position);

      aur_manager_send_seek (manager, position);
      break;
    }
    case AUR_CONTROL_LANGUAGE:{
      const gchar *language = find_param_str ("language", query, post_params);
      aur_manager_send_language (manager, language);
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

  manager->json = json_parser_new ();
}

static void
aur_manager_constructed (GObject * object)
{
  AurManager *manager = (AurManager *) (object);
  gchar *db_file;
  int aur_port;

  if (G_OBJECT_CLASS (aur_manager_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (aur_manager_parent_class)->constructed (object);

  g_object_get (manager->config, "aur-port", &aur_port, NULL);
  g_object_get (manager->config, "rtsp-port", &manager->rtsp_port, NULL);

  manager->rtsp = create_rtsp_server (manager);
  manager->receiver = aur_receiver_new (manager->rtsp);

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
aur_manager_dispose (GObject * object)
{
  AurManager *manager = (AurManager *) (object);

  g_list_foreach (manager->clients, (GFunc) g_object_unref, NULL);
  g_list_free (manager->clients);
  manager->clients = NULL;

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

  g_object_unref (manager->json);
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

static void
add_rtsp_uri (AurManager * manager, guint resource_id, const gchar * source_uri)
{
  GstRTSPMountPoints *mounts;
  GstRTSPMediaFactoryURI *factory;
  gchar *rtsp_uri = g_strdup_printf ("/resource/%u", resource_id);

  mounts = gst_rtsp_server_get_mount_points (manager->rtsp);

  factory = GST_RTSP_MEDIA_FACTORY_URI (aur_rtsp_play_media_factory_new ());

  /* Set up the URI, and set as shared (all viewers see the same stream) */
  gst_rtsp_media_factory_uri_set_uri (factory, source_uri);
  gst_rtsp_media_factory_set_shared (GST_RTSP_MEDIA_FACTORY (factory), TRUE);

  /* attach the factory to the url */
  gst_rtsp_mount_points_add_factory (mounts, rtsp_uri,
      GST_RTSP_MEDIA_FACTORY (factory));

  g_object_unref (mounts);

  g_free (rtsp_uri);
}

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
#if GST_DISABLE_GST_DEBUG
    gchar *uri;
#endif

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
#if GST_DISABLE_GST_DEBUG
    uri = g_file_get_uri (file);
    GST_LOG ("Scanning file %s\n", uri);
    g_free (uri);
#endif
    aur_media_db_add_file (manager->media_db, file);
    g_object_unref (file);
  } while (TRUE);
  aur_media_db_commit_transaction (manager->media_db);

  g_print ("Finished scanning playlist. Read %u entries\n",
      manager->playlist->len);

  g_io_channel_unref (io);
}


AurManager *
aur_manager_new (const char *config_file)
{
  AurConfig *config;
  AurManager *manager;
  gchar *playlist_file;

  config = aur_config_new (config_file);
  if (config == NULL)
    return NULL;

  manager = g_object_new (AUR_TYPE_MANAGER, "config", config, NULL);

  g_object_get (config, "playlist", &playlist_file, NULL);
  if (playlist_file) {
    read_playlist_file (manager, playlist_file);
    g_free (playlist_file);
  }

  if (get_playlist_len (manager)) {
    char *rtsp_uri = g_strdup_printf ("file://%s",
        (gchar *) (g_ptr_array_index (manager->playlist, 0)));
    add_rtsp_uri (manager, 1, rtsp_uri);
    g_free (rtsp_uri);
  }

  if (!aur_server_start (manager->server)) {
    g_object_unref (manager);
    return NULL;
  }

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
  g_print ("Creating resource %u for %s\n", resource_id, file_uri);
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

  manager_send_event_to_clients (manager, AUR_COMPONENT_ROLE_ALL,
      manager_make_set_media_event (manager, manager->current_resource));
}

static void
aur_manager_send_play (AurManager * manager)
{
  GstClock *clock;
  AurEvent *event;

  /* Update base time to match length of time paused */
  g_object_get (manager->net_clock, "clock", &clock, NULL);
  manager->base_time =
      gst_clock_get_time (clock) - manager->position + (GST_SECOND / 30);
  gst_object_unref (clock);
  manager->position = 0;

  event = aur_event_new (gst_structure_new ("json",
          "msg-type", G_TYPE_STRING, "play",
          "base-time", G_TYPE_INT64, (gint64) (manager->base_time), NULL));

  manager_send_event_to_clients (manager,
      AUR_COMPONENT_ROLE_CONTROLLER | AUR_COMPONENT_ROLE_PLAYER, event);
}

static void
aur_manager_send_pause (AurManager * manager)
{
  GstClock *clock;
  GstClockTime now;
  AurEvent *event;

  g_object_get (manager->net_clock, "clock", &clock, NULL);
  now = gst_clock_get_time (clock);
  gst_object_unref (clock);

  /* Calculate how much of the current file we played up until now, and store */
  manager->position = now - manager->base_time + (GST_SECOND / 30);
  g_print ("Storing position %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (manager->position));

  event = aur_event_new (gst_structure_new ("json",
          "msg-type", G_TYPE_STRING, "pause",
          "position", G_TYPE_INT64, (gint64) manager->position, NULL));

  manager_send_event_to_clients (manager,
      AUR_COMPONENT_ROLE_CONTROLLER | AUR_COMPONENT_ROLE_PLAYER, event);
}

static void
aur_manager_adjust_client_volume (AurManager * manager, guint client_id,
    gdouble volume)
{
  AurEvent *event = NULL;
  AurClientProxy *proxy;

  proxy =
      get_client_proxy_by_id (manager, client_id, AUR_COMPONENT_ROLE_PLAYER);
  if (proxy == NULL)
    return;
  proxy->volume = volume;

  event = aur_event_new (gst_structure_new ("json",
          "msg-type", G_TYPE_STRING, "client-volume",
          "client-id", G_TYPE_INT64, (gint64) client_id,
          "level", G_TYPE_DOUBLE, volume, NULL));
  manager_send_event_to_clients (manager, AUR_COMPONENT_ROLE_CONTROLLER, event);

  /* Tell the player which volume to set */
  event = aur_event_new (gst_structure_new ("json",
          "msg-type", G_TYPE_STRING, "volume",
          "level", G_TYPE_DOUBLE, volume * manager->current_volume, NULL));
  manager_send_event_to_client (manager, proxy, AUR_COMPONENT_ROLE_PLAYER,
      event);
}

static void
aur_manager_adjust_client_setting (AurManager * manager, guint client_id,
    gboolean enable, gboolean record_enable)
{
  AurEvent *event = NULL;
  AurClientProxy *proxy;

  proxy =
      get_client_proxy_by_id (manager, client_id, AUR_COMPONENT_ROLE_PLAYER);
  if (proxy == NULL)
    return;

  proxy->enabled = enable;
  proxy->record_enabled = record_enable;
  event = aur_event_new (gst_structure_new ("json",
          "msg-type", G_TYPE_STRING, "client-setting",
          "client-id", G_TYPE_INT64, (gint64) client_id,
          "enabled", G_TYPE_BOOLEAN, enable,
          "record-enabled", G_TYPE_BOOLEAN, record_enable, NULL));
  manager_send_event_to_clients (manager, AUR_COMPONENT_ROLE_CONTROLLER, event);

  /* Tell the player to change setting */
  event = aur_event_new (gst_structure_new ("json",
          "msg-type", G_TYPE_STRING, "client-setting",
          "enabled", G_TYPE_BOOLEAN, enable,
          "record-enabled", G_TYPE_BOOLEAN, record_enable, NULL));
  manager_send_event_to_client (manager, proxy, AUR_COMPONENT_ROLE_PLAYER,
      event);

  /* Update recording status at the client */
  if (proxy->roles & AUR_COMPONENT_ROLE_CAPTURE) {
    event = manager_make_record_event (manager, proxy);
    manager_send_event_to_client (manager, proxy,
        AUR_COMPONENT_ROLE_CAPTURE, event);
  }
}

static void
aur_manager_adjust_volume (AurManager * manager, gdouble volume)
{
  AurEvent *event = NULL;
  GList *cur;

  manager->current_volume = volume;
  event = aur_event_new (gst_structure_new ("json",
          "msg-type", G_TYPE_STRING, "volume",
          "level", G_TYPE_DOUBLE, volume, NULL));
  manager_send_event_to_clients (manager, AUR_COMPONENT_ROLE_CONTROLLER, event);

  /* Send a volume adjustment to each player */
  for (cur = manager->clients; cur != NULL; cur = cur->next) {
    AurClientProxy *proxy = (AurClientProxy *) (cur->data);

    if (proxy->roles & AUR_COMPONENT_ROLE_PLAYER) {
      event = aur_event_new (gst_structure_new ("json",
              "msg-type", G_TYPE_STRING, "volume",
              "level", G_TYPE_DOUBLE, proxy->volume * volume, NULL));
      manager_send_event_to_client (manager, proxy,
          AUR_COMPONENT_ROLE_PLAYER, event);
    }
  }
}

static void
aur_manager_send_seek (AurManager * manager, GstClockTime position)
{
  GstClock *clock;
  GstClockTime now;
  AurEvent *event = NULL;

  g_object_get (manager->net_clock, "clock", &clock, NULL);
  now = gst_clock_get_time (clock);
  gst_object_unref (clock);

  manager->base_time = now - position + (GST_SECOND / 4);
  if (manager->paused)
    manager->position = position;

  event = aur_event_new (gst_structure_new ("json",
          "msg-type", G_TYPE_STRING, "seek",
          "base-time", G_TYPE_INT64, (gint64) manager->base_time,
          "position", G_TYPE_INT64, (gint64) position, NULL));

  manager_send_event_to_clients (manager, AUR_COMPONENT_ROLE_PLAYER, event);
}

static void
aur_manager_send_language (AurManager * manager, const gchar * language)
{
  AurEvent *event = NULL;

  g_free (manager->language);
  manager->language = g_strdup (language ? language : "en");

  event = aur_event_new (gst_structure_new ("json",
          "msg-type", G_TYPE_STRING, "language",
          "language", G_TYPE_STRING, manager->language, NULL));

  manager_send_event_to_clients (manager,
      AUR_COMPONENT_ROLE_PLAYER | AUR_COMPONENT_ROLE_CONTROLLER, event);
}

static AurEvent *
manager_make_set_media_event (AurManager * manager, guint resource_id)
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
  g_object_get (manager->config, "aur-port", &port, NULL);
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
#else
      "resource-protocol", G_TYPE_STRING, "rtsp",
#endif
      "resource-port", G_TYPE_INT, port,
      "resource-path", G_TYPE_STRING, resource_path,
      "base-time", G_TYPE_INT64, (gint64) (manager->base_time),
      "position", G_TYPE_INT64, (gint64) (position),
      "paused", G_TYPE_BOOLEAN, manager->paused,
      "language", G_TYPE_STRING, manager->language, NULL);

  g_free (resource_path);

  return aur_event_new (msg);
}

static AurEvent *
manager_make_record_event (AurManager * manager G_GNUC_UNUSED,
    AurClientProxy * proxy)
{
  GstStructure *msg;

  if (proxy->record_path == NULL)
    proxy->record_path =
        aur_receiver_get_record_dest (manager->receiver, proxy->id);

  g_return_val_if_fail (proxy->record_path != NULL, NULL);

  msg = gst_structure_new ("json", "msg-type", G_TYPE_STRING, "record",
      "enabled", G_TYPE_BOOLEAN, proxy->record_enabled,
      "record-port", G_TYPE_INT, manager->rtsp_port,
      "record-path", G_TYPE_STRING, proxy->record_path, NULL);

  return aur_event_new (msg);
}

static AurEvent *
manager_make_player_clients_changed_event (G_GNUC_UNUSED AurManager * manager)
{
  GstStructure *msg;
  msg = gst_structure_new ("json",
      "msg-type", G_TYPE_STRING, "player-clients-changed", NULL);

  return aur_event_new (msg);
}
