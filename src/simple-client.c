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

#include <glib.h>
#include <glib-unix.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <avahi-glib/glib-malloc.h>

#include <src/client/aur-client.h>

static GMainLoop *ml = NULL;

static gboolean
sigint_handler (G_GNUC_UNUSED void *data)
{
  g_print ("Exiting...\n");
  g_main_loop_quit (ml);

  return TRUE;
}

static gboolean
print_position (AurClient *client)
{
  GstElement * player = client->player;
  GstFormat format = GST_FORMAT_TIME;
  gint64 pos;

  if (!aur_client_is_playing (client))
    goto end;

  if (gst_element_query_position (player, format, &pos)) {
    GstClock * clock;
    GstClockTime base_time, stream_time, now;
    GstClockTimeDiff diff;

    if (format != GST_FORMAT_TIME)
      goto end;

    clock = gst_element_get_clock (player);
    if (!clock)
      goto end;

    now = gst_clock_get_time (clock);
    gst_object_unref (clock);
    base_time = gst_element_get_base_time (player);
    stream_time = now - base_time + client->position;
    diff = pos - stream_time;

    g_print ("Playback position %" GST_TIME_FORMAT " (now %" GST_TIME_FORMAT
        " base_time %" GST_TIME_FORMAT " stream_time %" GST_TIME_FORMAT
        ") Sync error: %" GST_STIME_FORMAT "\n", GST_TIME_ARGS (pos),
        GST_TIME_ARGS (now), GST_TIME_ARGS (base_time),
        GST_TIME_ARGS (stream_time), GST_STIME_ARGS (diff));
  }

end:
  return TRUE;
}

static void
player_disposed (gpointer user_data,
    G_GNUC_UNUSED GObject * where_the_object_was)
{
  guint timeout = GPOINTER_TO_UINT (user_data);
  g_source_remove (timeout);
}

static void
on_eos_msg (AurClient *client, G_GNUC_UNUSED GstMessage * msg)
{
  SoupMessage *soup_msg;
  /* FIXME: Next song should all be handled server side */
  char *url = g_strdup_printf ("http://%s:%u/control/next",
      client->connected_server, client->connected_port);

  g_print ("Got EOS message\n");

#if 0
  soup_msg = soup_message_new ("GET", url);
  soup_session_queue_message (client->soup, soup_msg);
#endif
  g_free (url);
}

static void
player_created (AurClient *client, GstElement * player)
{
  GstBus *bus;
  guint timeout;

  timeout = g_timeout_add_seconds (1, (GSourceFunc) print_position, client);
  g_object_weak_ref (G_OBJECT (player), player_disposed,
      GUINT_TO_POINTER (timeout));

  bus = gst_element_get_bus (GST_ELEMENT (client->player));
  g_signal_connect_swapped (bus, "message::eos", G_CALLBACK (on_eos_msg),
      client);
  gst_object_unref (bus);
}

int
main (int argc, char *argv[])
{
  AurClient *client = NULL;
  int ret = 1;
  const gchar *server = NULL;

  gst_init (&argc, &argv);

  if (argc > 1) {
    /* Connect directly to the requested server, no avahi */
    server = argv[1];
  }

  avahi_set_allocator (avahi_glib_allocator ());

  g_unix_signal_add (SIGINT, sigint_handler, NULL);

  client = aur_client_new (NULL, server, AUR_CLIENT_PLAYER|AUR_CLIENT_CAPTURE);
  if (client == NULL)
    goto fail;

  g_signal_connect (client, "player-created", G_CALLBACK (player_created),
      NULL);

  ml = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (ml);

  ret = 0;
fail:
  if (client)
    g_object_unref (client);
  if (ml)
    g_main_loop_unref (ml);
  return ret;
}
