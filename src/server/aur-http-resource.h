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

#ifndef __AUR_HTTP_RESOURCE_H__
#define __AUR_HTTP_RESOURCE_H__

#include <gst/gst.h>
#include <gst/net/gstnet.h>
#include <libsoup/soup-types.h>

#include <src/server/aur-server-types.h>

G_BEGIN_DECLS

#define AUR_TYPE_HTTP_RESOURCE (aur_http_resource_get_type ())

typedef struct _AurHttpResourceClass AurHttpResourceClass;

struct _AurHttpResource
{
  GObject parent;

  GFile *source_file;
  guint use_count;
  GMappedFile *data;
};

struct _AurHttpResourceClass
{
  GObjectClass parent;
};

GType aur_http_resource_get_type(void);

void aur_http_resource_new_transfer (AurHttpResource *resource, SoupMessage *msg);

G_END_DECLS
#endif
