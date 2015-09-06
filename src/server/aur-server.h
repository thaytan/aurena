/* GStreamer
 * Copyright (C) 2012-2014 Jan Schmidt <thaytan@noraisin.net>
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

#ifndef __AUR_SERVER_H__
#define __AUR_SERVER_H__

#include <gst/gst.h>
#include <gst/net/gstnet.h>
#include <libsoup/soup.h>

#include <src/server/aur-server-types.h>

G_BEGIN_DECLS

#define AUR_TYPE_SERVER (aur_server_get_type ())

typedef struct _AurServerClass AurServerClass;

struct _AurServer
{
  GObject parent;
  SoupServer *soup;

  GHashTable *resources;

  AurHttpResource *(*get_resource)(AurServer *server, guint resource_id, void *cb_data);
  void *get_resource_userdata;

  AurConfig *config;
};

struct _AurServerClass
{
  GObjectClass parent;
};

GType aur_server_get_type(void);

void aur_server_set_resource_callback (AurServer *server, void *userdata);
gboolean aur_server_start (AurServer *server);
void aur_server_stop (AurServer *server);
void aur_server_set_resource_cb (AurServer *server, 
  AurHttpResource *(*get_resource)(AurServer *server, guint resource_id, void *cb_data), void *userdata);

void aur_server_add_handler (AurServer *server, const gchar *path, SoupServerCallback callback, gpointer user_data, GDestroyNotify destroy_notify);

G_END_DECLS
#endif
