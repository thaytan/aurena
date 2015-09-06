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

#ifndef __AUR_CONFIG_H__
#define __AUR_CONFIG_H__

#include <glib.h>
#include <glib-object.h>

#include <src/common/aur-types.h>

G_BEGIN_DECLS

#define AUR_TYPE_CONFIG (aur_config_get_type ())

typedef struct _AurConfigClass AurConfigClass;

struct _AurConfig
{
  GObject parent;

  gchar *config_file;

  int aur_port;
  int rtsp_port;

  gchar *database_location;
  gchar *playlist_location;
};

struct _AurConfigClass
{
  GObjectClass parent;
};

GType aur_config_get_type(void);
AurConfig *aur_config_new(const char *config_path);

G_END_DECLS

#endif
