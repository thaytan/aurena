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

#include <gst/gst.h>
#include <libs/gst/net/gstnet.h>
#include <gst/rtsp-server/rtsp-server.h>

#include "snra-types.h"
#include "snra-avahi.h"

G_BEGIN_DECLS

#define SNRA_TYPE_MANAGER (snra_manager_get_type ())

typedef struct _SnraManagerClass SnraManagerClass;

struct _SnraManager
{
  GObject parent;

  SnraServer *server;
  GstNetTimeProvider *net_clock;
  GstRTSPServer *rtsp;
  int rtsp_port;

  SnraAvahi *avahi;

  GPtrArray *playlist;
};

struct _SnraManagerClass
{
  GObjectClass parent;
};

GType snra_manager_get_type(void);
SnraManager *snra_manager_new(const char *playlist);

G_END_DECLS
#endif
