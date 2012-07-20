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

G_DEFINE_TYPE (SnraHttpResource, snra_http_resource, G_TYPE_OBJECT);

enum
{
  PROP_0,
  PROP_SOURCE_PATH,
  PROP_LAST
};

static void snra_http_resource_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void snra_http_resource_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

typedef struct _SnraTransfer
{
  SnraHttpResource *resource;
} SnraTransfer;

static gboolean
snra_http_resource_open(SnraHttpResource *resource)
{
  if (resource->data == NULL) {
    GError *error = NULL;
    g_print ("Opening resource %s\n", resource->source_path);
    resource->data = g_mapped_file_new (resource->source_path, FALSE, &error);
    if (resource->data == NULL) {
      g_message ("Failed to open resource %s: %s", resource->source_path, error->message);
      g_error_free (error);
      return FALSE;
    }
  }
  resource->use_count++;
  return TRUE;
}

static void
snra_http_resource_close(SnraHttpResource *resource)
{
  if (resource->use_count) {
    resource->use_count--;
    if (resource->use_count == 0) {
      g_print ("Releasing resource %s\n", resource->source_path);
#if GLIB_CHECK_VERSION(2,22,0)
      g_mapped_file_unref (resource->data);
#else
      g_mapped_file_free (resource->data);
#endif
      resource->data = NULL;
    }
  }
}

static void
resource_wrote_body (G_GNUC_UNUSED SoupMessage *msg, SnraTransfer *transfer)
{
  g_print ("Got wrote-body for %s. Use count now %d\n", transfer->resource->source_path, transfer->resource->use_count);
}

static void
resource_finished (G_GNUC_UNUSED SoupMessage *msg, SnraTransfer *transfer)
{
  g_print ("Completed transfer of %s. Use count now %d\n", transfer->resource->source_path, transfer->resource->use_count-1);

  /* Close the resource, destroy the transfer */
  snra_http_resource_close(transfer->resource);

  g_object_unref (transfer->resource);
  g_free (transfer);

}

void
snra_http_resource_new_transfer (SnraHttpResource *resource, SoupMessage *msg)
{
  /* Create a new transfer structure, and pass the contents of our resource to it */
  SnraTransfer *transfer;

  if (!snra_http_resource_open(resource)) {
    soup_message_set_status (msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
    return;
  }

  transfer = g_new0 (SnraTransfer, 1);
  transfer->resource = g_object_ref (resource);

  {
    const gsize len = g_mapped_file_get_length (transfer->resource->data);
    gchar *chunk = g_mapped_file_get_contents (transfer->resource->data); 

    g_signal_connect (msg, "finished", G_CALLBACK (resource_finished), transfer);
    g_signal_connect (msg, "wrote-body", G_CALLBACK (resource_wrote_body), transfer);

    soup_message_set_status (msg, SOUP_STATUS_OK);
    soup_message_headers_set_encoding (msg->response_headers,
        SOUP_ENCODING_CONTENT_LENGTH);
    soup_message_headers_replace (msg->response_headers, "Content-Type",
        snra_resource_get_mime_type(resource->source_path));

    soup_message_headers_set_content_length (msg->response_headers, len);
    soup_message_body_append (msg->response_body, SOUP_MEMORY_TEMPORARY, chunk, len);
    soup_message_body_complete (msg->response_body);
  }
}

static void
snra_http_resource_init (G_GNUC_UNUSED SnraHttpResource *resource)
{
}

static void
snra_http_resource_class_init (SnraHttpResourceClass *resource_class)
{
  GObjectClass *gobject_class = (GObjectClass *)(resource_class);

  gobject_class->set_property = snra_http_resource_set_property;
  gobject_class->get_property = snra_http_resource_get_property;

  g_object_class_install_property (gobject_class, PROP_SOURCE_PATH,
    g_param_spec_string ("source-path", "Source Path",
                         "Source file path resource",
                         NULL,
                         G_PARAM_READWRITE));
}

static void
snra_http_resource_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  SnraHttpResource *resource = (SnraHttpResource *)(object);

  switch (prop_id) {
    case PROP_SOURCE_PATH:
      g_free (resource->source_path);
      resource->source_path = g_value_dup_string (value);
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
  SnraHttpResource *resource = (SnraHttpResource *)(object);

  switch (prop_id) {
    case PROP_SOURCE_PATH:
      g_value_set_string (value, resource->source_path);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
