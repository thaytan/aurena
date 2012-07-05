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

#ifndef __SNRA_SERVER_H__
#define __SNRA_SERVER_H__

#include <gst/gst.h>
#include <libs/gst/net/gstnet.h>
#include <libsoup/soup-types.h>

#include "snra-types.h"

G_BEGIN_DECLS

#define SNRA_TYPE_SERVER (snra_server_get_type ())

typedef struct _SnraServerClass SnraServerClass;

struct _SnraServer
{
  GObject parent;
  SoupServer *soup;
  int port;
  int rtsp_port;

  GstClockTime base_time;
  GstNetTimeProvider *net_clock;

  GHashTable *resources;

  const gchar *test_path;
};

struct _SnraServerClass
{
  GObjectClass parent;
};

GType snra_server_get_type(void);

void snra_server_set_base_time(SnraServer *server, GstClockTime base_time);
void snra_server_set_clock (SnraServer *server, GstNetTimeProvider *net_clock);

G_END_DECLS
#endif
