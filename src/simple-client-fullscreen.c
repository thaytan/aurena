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
#include <gtk/gtk.h>
#include <gdk/gdkx.h>  // for GDK_WINDOW_XID

#include <gst/gst.h>

#if GST_CHECK_VERSION (0, 11, 1)
#include <gst/video/videooverlay.h>
#else
#include <gst/interfaces/xoverlay.h>
#endif

#include <src/client/snra-client.h>

static GMainLoop *ml = NULL;


static gboolean
quit_clicked (G_GNUC_UNUSED gpointer instance)
{
  g_print ("Exiting...\n");
  g_main_loop_quit (ml);

  return TRUE;
}

#if ! GST_CHECK_VERSION (0, 11, 1)
static GstBusSyncReply
bus_sync_handler (G_GNUC_UNUSED GstBus *bus, GstMessage *message,
    gpointer userdata)
{
  if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT)
    return GST_BUS_PASS;

  if (!gst_structure_has_name (message->structure, "prepare-xwindow-id"))
    return GST_BUS_PASS;

  gst_x_overlay_set_window_handle (GST_X_OVERLAY (GST_MESSAGE_SRC (message)),
      GPOINTER_TO_UINT (userdata));

  return GST_BUS_PASS;
}
#endif

static void
player_created (G_GNUC_UNUSED SnraClient *client, GstElement *playbin,
    GtkWidget *window)
{
#if  GST_CHECK_VERSION (0, 11, 1)
  gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (playbin),
      GDK_WINDOW_XID (gtk_widget_get_window (window)));
#else
  GstBus *bus;
  guintptr window_handle;

  window_handle = GDK_WINDOW_XID (gtk_widget_get_window (window));

  bus = gst_element_get_bus (playbin);
  gst_bus_set_sync_handler (bus, bus_sync_handler,
      GUINT_TO_POINTER (window_handle));
  gst_object_unref (bus);
#endif

  g_print ("Player created\n");
}

int
main (int argc, char *argv[])
{
  SnraClient *client = NULL;
  int ret = 1;
  const gchar *server = NULL;
  GOptionContext *ctx;
  GError *error = NULL;
  GtkWidget *window, *eventbox, *button;

  ctx = g_option_context_new ("Aurena fullscreen client");

  g_option_context_add_group (ctx, gtk_get_option_group(TRUE));
  g_option_context_add_group (ctx, gst_init_get_option_group());
  if (!g_option_context_parse (ctx, &argc, &argv, &error)) {
    g_print ("Arguments error: %s", error->message);
    return 1;
  }
  g_option_context_free (ctx);

  if (argc > 1) {
    /* Connect directly to the requested server, no avahi */
    server = argv[1];
  }

  avahi_set_allocator (avahi_glib_allocator ());

  client = snra_client_new (server, SNRA_CLIENT_PLAYER);
  if (client == NULL)
    goto fail;

  g_object_set (gtk_settings_get_default (),
      "gtk-application-prefer-dark-theme", TRUE, NULL);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_widget_set_double_buffered (window, FALSE);
  gtk_widget_set_redraw_on_allocate (window, FALSE);
  gtk_widget_set_app_paintable (window, TRUE);

  g_signal_connect_after (window, "delete-event", G_CALLBACK (quit_clicked),
      NULL);

  eventbox = gtk_event_box_new ();
  g_object_set (eventbox,
      "halign", GTK_ALIGN_END,
      "valign", GTK_ALIGN_START,
      "expand", FALSE,
      NULL);
  gtk_widget_set_double_buffered (eventbox, TRUE);
  gtk_widget_set_redraw_on_allocate (eventbox, TRUE);
  gtk_widget_set_app_paintable (eventbox, FALSE);
    gtk_container_add (GTK_CONTAINER (window), eventbox);

  button = gtk_button_new_from_stock (GTK_STOCK_QUIT);
  gtk_container_add (GTK_CONTAINER (eventbox), button);
  g_signal_connect (button, "clicked", G_CALLBACK (quit_clicked), NULL);

  gtk_window_fullscreen (GTK_WINDOW (window));
  gtk_widget_show_all (window);

  g_signal_connect (client, "player-created", G_CALLBACK(player_created),
		   window);


  gtk_widget_realize (button);
  gdk_window_ensure_native (gtk_widget_get_window (eventbox));

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
