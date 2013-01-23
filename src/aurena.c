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
#include <gst/gst.h>
#include <glib-unix.h>

#include "daemon/snra-manager.h"

GMainLoop *ml;

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
  SnraManager *manager;
  char *config_file = NULL;

  gst_init (&argc, &argv);

  if (argc > 1) 
    config_file = argv[1];

  g_unix_signal_add (SIGINT, sigint_handler, NULL);

  manager = snra_manager_new(config_file);
  if (manager == NULL)
    return -1;

  ml = g_main_loop_new(NULL, FALSE);
  g_main_loop_run(ml);

  g_object_unref (manager);
  return 0;
}
