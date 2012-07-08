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

G_DEFINE_TYPE (SnraManager, snra_manager, G_TYPE_OBJECT);

static void
snra_manager_init (SnraManager *manager)
{
}

static void
snra_manager_class_init (SnraManagerClass *manager_class)
{
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

static GstRTSPServer *
setup_rtsp (SnraManager *mgr, const char *uri)
{
  GstRTSPServer *server = NULL;
  GstRTSPMediaMapping *mapping;
  GstRTSPMediaFactoryURI *factory;

  server = gst_rtsp_server_new ();
  gst_rtsp_server_set_service (server, "5458");
  mapping = gst_rtsp_server_get_media_mapping (server);
  factory = gst_rtsp_media_factory_uri_new ();

  /* Set up the URI, and set as shared (all viewers see the same stream) */
  gst_rtsp_media_factory_uri_set_uri (factory, uri);
  gst_rtsp_media_factory_set_shared ( GST_RTSP_MEDIA_FACTORY (factory), TRUE);

  g_signal_connect (factory, "media-constructed", G_CALLBACK (new_stream_constructed_cb), mgr);

  /* attach the test factory to the test url */
  gst_rtsp_media_mapping_add_factory (mapping, "/resource/1",
      GST_RTSP_MEDIA_FACTORY (factory));

  g_object_unref (mapping);

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

SnraManager *
snra_manager_new(const char *test_path)
{
  SnraManager *manager = g_object_new (SNRA_TYPE_MANAGER, NULL);
  char *rtsp_uri;

  manager->net_clock = create_net_clock();
  rtsp_uri = g_strdup_printf("file://%s", test_path);
  manager->rtsp = setup_rtsp(manager, rtsp_uri);
  g_free (rtsp_uri);

  manager->avahi = g_object_new (SNRA_TYPE_AVAHI, NULL);

  manager->server = g_object_new (SNRA_TYPE_SERVER,
      "rtsp-port", 5458, "clock", manager->net_clock, NULL);
  /* FIXME: Implement a mapping for multiple resources */
  manager->server->test_path = test_path;

  return manager;
}
