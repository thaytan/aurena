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

#include <src/client/snra-client.h>

static GMainLoop *ml = NULL;

static gboolean
sigint_handler (G_GNUC_UNUSED void *data)
{
  g_print ("Exiting...\n");
  g_main_loop_quit (ml);

  return TRUE;
}

int
main (int argc, char *argv[])
{
  SnraClient *client = NULL;
  int ret = 1;
  const gchar *server = NULL;

  gst_init (&argc, &argv);

  if (argc > 1) {
    /* Connect directly to the requested server, no avahi */
    server = argv[1];
  }

  avahi_set_allocator (avahi_glib_allocator ());

  g_unix_signal_add (SIGINT, sigint_handler, NULL);

  client = snra_client_new (server);
  if (client == NULL)
    goto fail;

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
