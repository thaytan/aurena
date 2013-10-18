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
#ifndef __SNRA_MANAGER_H__
#define __SNRA_MANAGER_H__

#include <stdio.h>
#include <gst/gst.h>
#include <gst/net/gstnet.h>
#include <libsoup/soup-types.h>

#ifdef HAVE_GST_RTSP
#include <gst/rtsp-server/rtsp-server.h>
#endif

#include <src/snra-types.h>
#include "snra-avahi.h"

G_BEGIN_DECLS

#define SNRA_TYPE_MANAGER (snra_manager_get_type ())

typedef struct _SnraManagerClass SnraManagerClass;

struct _SnraManager
{
  GObject parent;

  SnraServer *server;
  GstNetTimeProvider *net_clock;
#ifdef HAVE_GST_RTSP
  GstRTSPServer *rtsp;
#endif
  int rtsp_port;

  SnraAvahi *avahi;

  SnraConfig *config;
  SnraMediaDB *media_db;

  GPtrArray *playlist;
  gboolean paused;
  guint current_resource;
  GFile *custom_file;
  gchar *language;

  guint next_player_id;
  GList *player_info;

  GList *ctrl_clients;

  GstClockTime base_time;
  GstClockTime position;

  gdouble current_volume;

  guint ping_timeout;
};

struct _SnraManagerClass
{
  GObjectClass parent;
};

GType snra_manager_get_type(void);
SnraManager *snra_manager_new(const char *config_file);

G_END_DECLS
#endif
