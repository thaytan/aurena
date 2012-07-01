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

#include <gst/gst.h>
#include <libs/gst/net/gstnet.h>
#include <gst/rtsp-server/rtsp-server.h>

GMainLoop *ml;
GstNetTimeProvider *net_time;

static gboolean
timeout (GstRTSPServer * server, gboolean ignored)
{
  GstRTSPSessionPool *pool;

  pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_cleanup (pool);
  g_object_unref (pool);

  g_print ("Exiting on timeout...\n");

  g_main_loop_quit(ml);

  return TRUE;
}

int
setup_rtsp (char *uri)
{
  GstRTSPServer *server;
  GstRTSPMediaMapping *mapping;
  GstRTSPMediaFactoryURI *factory;

  server = gst_rtsp_server_new ();
  mapping = gst_rtsp_server_get_media_mapping (server);
  factory = gst_rtsp_media_factory_uri_new ();

  /* Set up the URI, and set as shared (all viewers see the same stream) */
  gst_rtsp_media_factory_uri_set_uri (factory, uri);
  gst_rtsp_media_factory_set_shared ( GST_RTSP_MEDIA_FACTORY (factory), TRUE);

  /* attach the test factory to the /test url */
  gst_rtsp_media_mapping_add_factory (mapping, "/test",
      GST_RTSP_MEDIA_FACTORY (factory));

  g_object_unref (mapping);

  /* attach the server to the default maincontext */
  if (gst_rtsp_server_attach (server, NULL) == 0)
    goto failed;

  g_timeout_add_seconds (60, (GSourceFunc) timeout, server);

  /* start serving */
  g_print ("stream ready at rtsp://127.0.0.1:8554/test\n");

  return 0;

  /* ERRORS */
failed:
  {
    g_print ("failed to attach the server\n");
    return -1;
  }
}

int
setup_net_clock()
{
  GstClock *clock;

  clock = gst_system_clock_obtain();
  net_time = gst_net_time_provider_new (clock, NULL, 0);
  gst_object_unref (clock);

  return 0;
}

int
main (int argc, char *argv[])
{

  gst_init (&argc, &argv);

  if (argc < 2) {
    g_message ("usage: %s <uri>", argv[0]);
    return -1;
  }

  if (setup_net_clock() < 0)
    return -1;

  if (setup_rtsp (argv[1]) < 0)
    return -1;

  ml = g_main_loop_new(NULL, FALSE);
  g_main_loop_run(ml);

  return 0;
}
