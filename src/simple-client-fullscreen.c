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

#include <gst/video/videooverlay.h>

#include <src/client/aur-client.h>

static GMainLoop *ml = NULL;


static gboolean
quit_clicked (G_GNUC_UNUSED gpointer instance)
{
  g_print ("Exiting...\n");
  g_main_loop_quit (ml);

  return TRUE;
}

static void
player_created (G_GNUC_UNUSED AurClient *client, GstElement *playbin,
    GtkWidget *window)
{
  gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (playbin),
      GDK_WINDOW_XID (gtk_widget_get_window (window)));

  g_print ("Player created\n");
}

int
main (int argc, char *argv[])
{
  AurClient *client = NULL;
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

  client = aur_client_new (NULL, server, AUR_CLIENT_PLAYER, g_get_host_name ());
  if (client == NULL)
    goto fail;

  g_object_set (gtk_settings_get_default (),
      "gtk-application-prefer-dark-theme", TRUE, NULL);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
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
  gtk_widget_set_redraw_on_allocate (eventbox, TRUE);
  gtk_widget_set_app_paintable (eventbox, FALSE);
    gtk_container_add (GTK_CONTAINER (window), eventbox);

  button = gtk_button_new_from_icon_name ("application-exit", GTK_ICON_SIZE_BUTTON);
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
