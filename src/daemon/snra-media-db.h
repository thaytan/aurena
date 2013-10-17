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

#ifndef __SNRA_MEDIA_DB_H__
#define __SNRA_MEDIA_DB_H__

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <sqlite3.h>

#include <src/snra-types.h>

G_BEGIN_DECLS

typedef struct _SnraMediaDBPriv SnraMediaDBPriv;

struct _SnraMediaDB
{
  GObject parent;
  SnraMediaDBPriv *priv;
};

SnraMediaDB *snra_media_db_new(const char *db_path);
void snra_media_db_add_file (SnraMediaDB *media_db, GFile *file);
guint snra_media_db_get_file_count (SnraMediaDB *media_db);
GFile *snra_media_db_get_file_by_id (SnraMediaDB *media_db, guint id);

G_END_DECLS

#endif
