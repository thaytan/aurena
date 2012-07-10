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
 * Sonarea Manager is the central object which:
 *   creates the network clock
 *   Establishes libsoup session
 *   Creates RTSP sessions as needed
 *   Distributes the network clock and base time to clients
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "snra-manager.h"
#include "snra-server.h"
#include "snra-http-resource.h"

G_DEFINE_TYPE (SnraManager, snra_manager, G_TYPE_OBJECT);

static void snra_manager_finalize(GObject *object);
static SnraHttpResource * snra_manager_get_resource_cb (SnraServer *server, guint resource_id, void *userdata);

static GstNetTimeProvider *
create_net_clock()
{
  GstClock *clock;
  GstNetTimeProvider *net_time;

  clock = gst_system_clock_obtain();
  net_time = gst_net_time_provider_new (clock, NULL, 0);
  gst_object_unref (clock);

  return net_time;
}

static GstRTSPServer *
create_rtsp_server (SnraManager *mgr)
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

static void
snra_manager_init (SnraManager *manager)
{
  manager->playlist = g_ptr_array_new_with_free_func (g_free);
  manager->rtsp_port = 5458;
  manager->net_clock = create_net_clock();
  manager->rtsp = create_rtsp_server(manager);

  manager->avahi = g_object_new (SNRA_TYPE_AVAHI, NULL);

  manager->server = g_object_new (SNRA_TYPE_SERVER,
      "rtsp-port", manager->rtsp_port, "clock", manager->net_clock, NULL);
  snra_server_set_resource_cb (manager->server, snra_manager_get_resource_cb, manager);
}

static void
snra_manager_class_init (SnraManagerClass *manager_class)
{
  GObjectClass *object_class = (GObjectClass *)(manager_class);

  object_class->finalize = snra_manager_finalize;
}

static void
snra_manager_finalize(GObject *object)
{
  SnraManager *manager = (SnraManager *)(object);

  g_ptr_array_free (manager->playlist, TRUE);
}

static void
rtsp_media_prepared(GstRTSPMedia *media, SnraManager *mgr)
{
  g_object_set (media->rtpbin, "use-pipeline-clock", TRUE, NULL);
}

static void
new_stream_constructed_cb (GstRTSPMediaFactory *factory,
    GstRTSPMedia *media, SnraManager *mgr)
{
  g_print ("Media constructed: %p\n", media);
  // snra_server_set_base_time (mgr->server, GST_CLOCK_TIME_NONE);
  g_signal_connect (media, "prepared", G_CALLBACK (rtsp_media_prepared), mgr);

}


static void
add_rtsp_uri (SnraManager *manager, guint resource_id, const gchar *source_uri)
{
  GstRTSPMediaMapping *mapping;
  GstRTSPMediaFactoryURI *factory;
  gchar *rtsp_uri = g_strdup_printf ("/resource/%u", resource_id);

  mapping = gst_rtsp_server_get_media_mapping (manager->rtsp);
  factory = gst_rtsp_media_factory_uri_new ();
  /* Set up the URI, and set as shared (all viewers see the same stream) */
  gst_rtsp_media_factory_uri_set_uri (factory, source_uri);
  gst_rtsp_media_factory_set_shared ( GST_RTSP_MEDIA_FACTORY (factory), TRUE);
  g_signal_connect (factory, "media-constructed", G_CALLBACK (new_stream_constructed_cb), manager);
  /* attach the test factory to the test url */
  gst_rtsp_media_mapping_add_factory (mapping, rtsp_uri, GST_RTSP_MEDIA_FACTORY (factory));
  g_object_unref (mapping);

  g_free (rtsp_uri);
}

static void
read_playlist_file(SnraManager *manager, const char *filename)
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
    result = g_io_channel_read_line(io, &line, NULL, NULL, NULL);
    if (result == G_IO_STATUS_AGAIN)
      continue;
    if (result != G_IO_STATUS_NORMAL) 
      break;
    g_strchomp (line);
    g_ptr_array_add (manager->playlist, line);
  } while (TRUE);

  g_print ("Read %u entries\n", manager->playlist->len);

  g_io_channel_unref (io); 
}


SnraManager *
snra_manager_new(const char *playlist_file)
{
  SnraManager *manager = g_object_new (SNRA_TYPE_MANAGER, NULL);

  read_playlist_file (manager, playlist_file);

  if (manager->playlist->len) {
    char *rtsp_uri = g_strdup_printf("file://%s", g_ptr_array_index (manager->playlist, 0));
    add_rtsp_uri (manager, 1, rtsp_uri);
    g_free (rtsp_uri);

    manager->server->current_resource = g_random_int_range (0, manager->playlist->len) + 1;
  }

  snra_server_start (manager->server);

  return manager;
}

static SnraHttpResource *
snra_manager_get_resource_cb (SnraServer *server, guint resource_id, void *userdata)
{
  SnraManager *manager = (SnraManager *)(userdata);
  const gchar *file;

  if (resource_id < 1 || resource_id > manager->playlist->len)
    return NULL;

  file = g_ptr_array_index (manager->playlist, resource_id - 1);

  g_print ("Creating resource %u for %s\n", resource_id, file);

  return g_object_new (SNRA_TYPE_HTTP_RESOURCE, "source-path", file, NULL);
}
