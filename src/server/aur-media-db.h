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

#ifndef __AUR_MEDIA_DB_H__
#define __AUR_MEDIA_DB_H__

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <sqlite3.h>

#include <server/aur-server-types.h>

G_BEGIN_DECLS

typedef struct _AurMediaDBPrivate AurMediaDBPrivate;

struct _AurMediaDB
{
  GObject parent;
  AurMediaDBPrivate *priv;
};

AurMediaDB *aur_media_db_new(const char *db_path);
void aur_media_db_add_file (AurMediaDB *media_db, GFile *file);
guint aur_media_db_get_file_count (AurMediaDB *media_db);
GFile *aur_media_db_get_file_by_id (AurMediaDB *media_db, guint id);
void aur_media_db_begin_transaction (AurMediaDB *media_db);
void aur_media_db_commit_transaction (AurMediaDB *media_db);

G_END_DECLS

#endif
