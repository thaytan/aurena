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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>
#include <libsoup/soup-server.h>
#include <libsoup/soup-message.h>
#include <libsoup/soup-socket.h>
#include <libsoup/soup-address.h>

#include <common/aur-config.h>

#include "aur-server.h"
#include "aur-http-client.h"
#include "aur-resource.h"
#include "aur-http-resource.h"

G_DEFINE_TYPE (AurServer, aur_server, G_TYPE_OBJECT);

enum
{
  PROP_0,
  PROP_CONFIG,
  PROP_CLOCK,
  PROP_LAST
};

static void aur_server_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void aur_server_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void aur_soup_message_set_redirect (SoupMessage * msg,
    guint status_code, const char *redirect_uri);
static void aur_server_finalize (GObject * object);
static void aur_server_dispose (GObject * object);

static void
aur_soup_message_set_redirect (SoupMessage * msg, guint status_code,
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

static AurHttpResource *
aur_server_get_resource (AurServer * server, guint resource_id)
{
  AurHttpResource *resource = NULL;

  resource =
      g_hash_table_lookup (server->resources, GINT_TO_POINTER (resource_id));
  if (resource == NULL) {
    resource =
        server->get_resource (server, resource_id,
        server->get_resource_userdata);
    if (resource_id != G_MAXUINT)
      g_hash_table_insert (server->resources, GINT_TO_POINTER (resource_id),
          resource);
  }
  return resource;
}

static void
server_resource_cb (G_GNUC_UNUSED SoupServer * soup, SoupMessage * msg,
    const char *path, G_GNUC_UNUSED GHashTable * query,
    G_GNUC_UNUSED SoupClientContext * client, AurServer * server)
{
  guint resource_id = 0;
  AurHttpResource *resource;

  if (!sscanf (path, "/resource/%u", &resource_id))
    goto error;

  resource = aur_server_get_resource (server, resource_id);
  if (resource == NULL)
    goto error;

  GST_DEBUG_OBJECT (server, "Hit on resource %u\n", resource_id);
  aur_http_resource_new_transfer (resource, msg);

  return;
error:
  soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
}

static GFile *
get_data_filename (const gchar * basename)
{
  const gchar *const *dirs;
  gchar *filepath = NULL;
  GFile *file;
  gint i;

  dirs = g_get_system_data_dirs ();

  /* Look in system paths for the file */
  for (i = 0; dirs[i] != NULL; i++) {
    filepath = g_build_filename (dirs[i], "aurena", "htdocs", basename, NULL);
    file = g_file_new_for_path (filepath);
    g_free (filepath);

    if (g_file_query_exists (file, NULL))
      break;

    g_object_unref (file);
    file = NULL;
  }

  /* Check uninstalled */
  if (file == NULL) {
    filepath =
        g_build_filename (g_get_current_dir (), "data", "htdocs", basename,
        NULL);
    file = g_file_new_for_path (filepath);
    if (!g_file_query_exists (file, NULL)) {
      g_object_unref (file);
      file = NULL;
    }
    g_free (filepath);
  }

  if (file &&
      g_file_query_file_type (file, G_FILE_QUERY_INFO_NONE,
          NULL) == G_FILE_TYPE_DIRECTORY) {
    GFile *tmp = g_file_get_child (file, "index.html");
    g_object_unref (file);
    file = tmp;
  }

  return file;
}

static void
server_file_cb (G_GNUC_UNUSED SoupServer * soup, SoupMessage * msg,
    const char *path, G_GNUC_UNUSED GHashTable * query,
    G_GNUC_UNUSED SoupClientContext * client, G_GNUC_UNUSED AurServer * server)
{
  gchar *file_path = NULL;
  GFile *filename = NULL;
  gchar *contents;
  gsize size;
  const gchar *mime_type;

  GST_LOG_OBJECT (server, "Request for path %s", path);

  if (!g_str_has_prefix (path, "/"))
    goto fail;

  if (path[1] == '\0') {
    aur_soup_message_set_redirect (msg, SOUP_STATUS_MOVED_PERMANENTLY, "/ui/");
    return;
  }

  filename = get_data_filename (path + 1);
  if (filename == NULL)
    goto fail;

  file_path = g_file_get_path (filename);
  if (!g_file_get_contents (file_path, &contents, &size, NULL)) {
    g_free (file_path);
    goto fail;
  }

  g_object_unref (filename);
  filename = NULL;

  mime_type = aur_resource_get_mime_type (file_path);
  GST_LOG_OBJECT (server, "Returning %s - %s", mime_type, file_path);

  g_free (file_path);

  soup_message_set_response (msg, mime_type, SOUP_MEMORY_TAKE, contents, size);
  soup_message_set_status (msg, SOUP_STATUS_OK);

  return;

fail:
  soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
}

static void
aur_server_init (AurServer * server)
{
  server->resources =
      g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, g_object_unref);
}

static void
aur_server_constructed (GObject * object)
{
  AurServer *server = (AurServer *) (object);
  //SoupSocket *socket;
  gint port;

  if (G_OBJECT_CLASS (aur_server_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (aur_server_parent_class)->constructed (object);

  g_object_get (server->config, "aur-port", &port, NULL);

  server->soup = soup_server_new (NULL, NULL);

  soup_server_add_handler (server->soup, "/",
      (SoupServerCallback) server_file_cb, g_object_ref (server),
      g_object_unref);

  soup_server_add_handler (server->soup, "/resource",
      (SoupServerCallback) server_resource_cb,
      g_object_ref (server), g_object_unref);

#if 0
  socket = soup_server_get_listener (server->soup);
  if (socket) {
    SoupAddress *addr;
    g_object_get (socket, SOUP_SOCKET_LOCAL_ADDRESS, &addr, NULL);
    g_print ("Now listening on %s:%u\n",
        soup_address_get_name (addr), soup_address_get_port (addr));
    g_object_unref (addr);
  }
#else
  g_print ("Server ready on port %u\n", port);
#endif
}

static void
aur_server_class_init (AurServerClass * server_class)
{
  GObjectClass *gobject_class = (GObjectClass *) (server_class);

  gobject_class->constructed = aur_server_constructed;
  gobject_class->dispose = aur_server_dispose;
  gobject_class->finalize = aur_server_finalize;
  gobject_class->set_property = aur_server_set_property;
  gobject_class->get_property = aur_server_get_property;

  g_object_class_install_property (gobject_class, PROP_CONFIG,
      g_param_spec_object ("config", "config",
          "Aurena service configuration object",
          AUR_TYPE_CONFIG, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, PROP_CLOCK,
      g_param_spec_object ("clock", "clock",
          "clock to synchronise playback",
          GST_TYPE_NET_TIME_PROVIDER, G_PARAM_READWRITE));
}

static void
aur_server_finalize (GObject * object)
{
  AurServer *server = (AurServer *) (object);
  g_object_unref (server->soup);
  g_hash_table_remove_all (server->resources);

  if (server->config)
    g_object_unref (server->config);

  G_OBJECT_CLASS (aur_server_parent_class)->finalize (object);
}

static void
aur_server_dispose (GObject * object)
{
  AurServer *server = (AurServer *) (object);

  soup_server_disconnect (server->soup);

  G_OBJECT_CLASS (aur_server_parent_class)->dispose (object);
}

static void
aur_server_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  AurServer *server = (AurServer *) (object);

  switch (prop_id) {
    case PROP_CONFIG:
      server->config = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
aur_server_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  AurServer *server = (AurServer *) (object);

  switch (prop_id) {
    case PROP_CONFIG:
      g_value_set_object (value, server->config);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
aur_server_start (AurServer * server)
{
  gint port;

  g_object_get (server->config, "aur-port", &port, NULL);
  return soup_server_listen_all (server->soup, port, 0, NULL);
}

void
aur_server_stop (AurServer * server)
{
  soup_server_disconnect (server->soup);
  /* some requests may still be in progress but no new
   * connections can happen
   */
}

void
aur_server_set_resource_cb (AurServer * server,
    AurHttpResource * (*callback) (AurServer * server, guint resource_id,
        void *cb_data), void *userdata)
{
  server->get_resource = callback;
  server->get_resource_userdata = userdata;
}

void
aur_server_add_handler (AurServer * server, const gchar * path,
    SoupServerCallback callback, gpointer user_data,
    GDestroyNotify destroy_notify)
{
  soup_server_add_handler (server->soup, path, callback,
      user_data, destroy_notify);
}
