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

#ifndef __SNRA_HTTP_RESOURCE_H__
#define __SNRA_HTTP_RESOURCE_H__

#include <gst/gst.h>
#include <libs/gst/net/gstnet.h>
#include <libsoup/soup-types.h>

#include "snra-types.h"

G_BEGIN_DECLS

#define SNRA_TYPE_HTTP_RESOURCE (snra_http_resource_get_type ())

typedef struct _SnraHttpResourceClass SnraHttpResourceClass;

struct _SnraHttpResource
{
  GObject parent;

  gchar *source_path;
  guint use_count;
  GMappedFile *data;
};

struct _SnraHttpResourceClass
{
  GObjectClass parent;
};

GType snra_http_resource_get_type(void);

void snra_http_resource_new_transfer (SnraHttpResource *resource, SnraServer *server, SoupMessage *msg);

G_END_DECLS
#endif
