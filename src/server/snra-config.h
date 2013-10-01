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

#ifndef __SNRA_CONFIG_H__
#define __SNRA_CONFIG_H__

#include <glib.h>
#include <glib-object.h>

#include <src/common/snra-types.h>

G_BEGIN_DECLS

#define SNRA_TYPE_CONFIG (snra_config_get_type ())

typedef struct _SnraConfigClass SnraConfigClass;

struct _SnraConfig
{
  GObject parent;

  gchar *config_file;

  int snra_port;
  int rtsp_port;

  gchar *database_location;
  gchar *playlist_location;
};

struct _SnraConfigClass
{
  GObjectClass parent;
};

GType snra_config_get_type(void);
SnraConfig *snra_config_new(const char *config_path);

G_END_DECLS

#endif
