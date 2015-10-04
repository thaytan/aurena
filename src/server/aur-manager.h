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
#ifndef __AUR_MANAGER_H__
#define __AUR_MANAGER_H__

#include <stdio.h>
#include <gst/gst.h>
#include <gst/net/gstnet.h>
#include <libsoup/soup-types.h>

#include <gst/rtsp-server/rtsp-server.h>

#include <server/aur-server-types.h>
#include "aur-avahi.h"

G_BEGIN_DECLS

#define AUR_TYPE_MANAGER (aur_manager_get_type ())

typedef struct _AurManagerClass AurManagerClass;

struct _AurManager
{
  GObject parent;

  AurServer *server;
  GstNetTimeProvider *net_clock;

  GstRTSPServer *rtsp;
  int rtsp_port;

  AurAvahi *avahi;

  AurConfig *config;
  AurMediaDB *media_db;

  GPtrArray *playlist;
  gboolean paused;
  guint current_resource;
  GFile *custom_file;
  gchar *language;

  guint next_player_id;
  GList *clients;

  GstClockTime base_time;
  GstClockTime position;

  gdouble current_volume;

  guint ping_timeout;

  AurReceiver *receiver;
};

struct _AurManagerClass
{
  GObjectClass parent;
};

GType aur_manager_get_type(void);
AurManager *aur_manager_new(const char *config_file);

G_END_DECLS
#endif
