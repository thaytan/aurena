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

#include "snra-resource.h"
#include "snra-http-resource.h"

#if 0
#define DEBUG_PRINT(...) g_print (__VA_ARGS__);
#else
#define DEBUG_PRINT(...)
#endif

G_DEFINE_TYPE (SnraHttpResource, snra_http_resource, G_TYPE_OBJECT);

enum
{
  PROP_0,
  PROP_SOURCE_FILE,
  PROP_LAST
};

static gint resources_open = 0;

static void snra_http_resource_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void snra_http_resource_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

typedef struct _SnraTransfer
{
  SnraHttpResource *resource;
} SnraTransfer;

static gboolean
snra_http_resource_open (SnraHttpResource * resource)
{
  if (resource->data == NULL) {
    GError *error = NULL;

    resources_open++;

    if (g_file_is_native (resource->source_file)) {
      gchar *local_path;

      local_path = g_file_get_path (resource->source_file);
      g_assert (local_path != NULL);

      DEBUG_PRINT ("Opening resource %s. %d now open\n", local_path,
          resources_open);

      resource->data = g_mapped_file_new (local_path, FALSE, &error);

      if (resource->data == NULL) {
        g_message ("Failed to open resource %s: %s", local_path,
            error->message);
        g_error_free (error);
        g_free (local_path);
        resources_open--;

        return FALSE;
      }

      g_free (local_path);
    } else {
      /* Non-native files can't be mmap()ped. */
      resource->data = NULL;
    }
  }
  g_object_ref (resource);
  resource->use_count++;
  return TRUE;
}

static void
snra_http_resource_close (SnraHttpResource * resource)
{
  if (resource->use_count) {
    resource->use_count--;
    if (resource->use_count == 0) {
      resources_open--;

      if (g_file_is_native (resource->source_file)) {
        gchar *local_path;

        /* Release the mmap() on the local file. */
        local_path = g_file_get_path (resource->source_file);
        DEBUG_PRINT ("Releasing resource %s. %d now open\n", local_path,
            resources_open);
#if GLIB_CHECK_VERSION(2,22,0)
        g_mapped_file_unref (resource->data);
#else
        g_mapped_file_free (resource->data);
#endif
        resource->data = NULL;

        DEBUG_PRINT ("closed resource %s (%p) use count now %d\n",
            local_path, resource, resource->use_count);
        g_free (local_path);
      } else {
        /* Non-native file. */
        resource->data = NULL;
      }
    }
  }

  g_object_unref (resource);
}

static SnraTransfer *
snra_transfer_new (SnraHttpResource *resource)
{
  SnraTransfer *transfer;

  if (!snra_http_resource_open (resource))
    return NULL;

  transfer = g_new0 (SnraTransfer, 1);
  transfer->resource = g_object_ref (resource);

  DEBUG_PRINT ("Started transfer with resource %p use count now %d\n",
      resource, resource->use_count);

  return transfer;
}

static void
snra_transfer_free (SnraTransfer *transfer)
{
  snra_http_resource_close (transfer->resource);

  DEBUG_PRINT ("Completed transfer of %p. Use count now %d\n",
      transfer->resource, transfer->resource->use_count);

  g_object_unref (transfer->resource);
  g_free (transfer);
}

void
snra_http_resource_new_transfer (SnraHttpResource * resource, SoupMessage * msg)
{
  /* Create a new transfer structure, and pass the contents of our
   * resource to it */
  SnraTransfer *transfer;
  SoupBuffer *buffer;
  gchar *local_path;

  /* Non-local files are implemented as a HTTP redirect.
   *
   * FIXME: This isn't an ideal solution, as clients may not be able to access
   * exactly the same resources as the server (due to network configuration or
   * access restrictions). It would be better to proxy the file through the
   * daemon, but that's an order of magnitude more complex than a simple HTTP
   * redirect. */
  if (!g_file_is_native (resource->source_file)) {
    gchar *resource_uri = g_file_get_uri (resource->source_file);
    soup_message_set_redirect (msg, SOUP_STATUS_TEMPORARY_REDIRECT, resource_uri);
    g_free (resource_uri);

    return;
  }

  transfer = snra_transfer_new (resource);

  if (!transfer) {
    soup_message_set_status (msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
    return;
  }

  local_path = g_file_get_path (resource->source_file);
  g_assert (local_path != NULL);

  buffer = soup_buffer_new_with_owner (
      g_mapped_file_get_contents (transfer->resource->data),
      g_mapped_file_get_length (transfer->resource->data),
      transfer, (GDestroyNotify) snra_transfer_free);

  soup_message_set_status (msg, SOUP_STATUS_OK);
  soup_message_headers_replace (msg->response_headers,
      "Content-Type", snra_resource_get_mime_type (local_path));
  soup_message_body_append_buffer (msg->response_body, buffer);
  soup_buffer_free (buffer);

  g_free (local_path);
}

static void
snra_http_resource_init (G_GNUC_UNUSED SnraHttpResource * resource)
{
}

static void
snra_http_resource_class_init (SnraHttpResourceClass * resource_class)
{
  GObjectClass *gobject_class = (GObjectClass *) (resource_class);

  gobject_class->set_property = snra_http_resource_set_property;
  gobject_class->get_property = snra_http_resource_get_property;

  g_object_class_install_property (gobject_class, PROP_SOURCE_FILE,
      g_param_spec_object ("source-file", "Source File",
          "Source file resource", G_TYPE_FILE, G_PARAM_READWRITE));
}

static void
snra_http_resource_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  SnraHttpResource *resource = (SnraHttpResource *) (object);

  switch (prop_id) {
    case PROP_SOURCE_FILE:
      g_clear_object (&resource->source_file);
      resource->source_file = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
snra_http_resource_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  SnraHttpResource *resource = (SnraHttpResource *) (object);

  switch (prop_id) {
    case PROP_SOURCE_FILE:
      g_value_set_object (value, resource->source_file);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
