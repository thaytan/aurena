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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "snra-media-db.h"

#define SNRA_TYPE_MEDIA_DB (snra_media_db_get_type ())

typedef struct _SnraMediaDBClass SnraMediaDBClass;

enum {
  PROP_0 = 0,
  PROP_DB_FILE,
  PROP_LAST
};

struct _SnraMediaDB
{
  GObject parent;

  sqlite3 *handle;
  gboolean errored;
  gchar *db_file;
};

struct _SnraMediaDBClass
{
  GObjectClass parent;
};

static GType snra_media_db_get_type(void);
static void snra_media_db_finalize(GObject *object);
static gboolean media_db_create_tables (SnraMediaDB *media_db);
static void snra_media_db_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void snra_media_db_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

G_DEFINE_TYPE (SnraMediaDB, snra_media_db, G_TYPE_OBJECT);

static void
snra_media_db_init (G_GNUC_UNUSED SnraMediaDB *media_db)
{
}

static void
snra_media_db_constructed (G_GNUC_UNUSED GObject *object)
{
  SnraMediaDB *media_db = (SnraMediaDB *)(object);
  sqlite3 *handle = NULL;

  if (G_OBJECT_CLASS (snra_media_db_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (snra_media_db_parent_class)->constructed (object);

  g_mkdir_with_parents (g_path_get_dirname (media_db->db_file), 0755); 

  if (sqlite3_open (media_db->db_file, &handle) != SQLITE_OK) {
    g_warning ("Could not open media DB %s\n", media_db->db_file);
    media_db->errored = TRUE;
  }

  media_db->handle = handle;

  if (!media_db_create_tables(media_db))
    media_db->errored = TRUE;
  g_print ("media DB ready at %s\n", media_db->db_file);
}

static void
snra_media_db_class_init (SnraMediaDBClass *media_db_class)
{
  GObjectClass *object_class = (GObjectClass *)(media_db_class);

  object_class->constructed = snra_media_db_constructed;
  object_class->set_property = snra_media_db_set_property;
  object_class->get_property = snra_media_db_get_property;

  object_class->finalize = snra_media_db_finalize;

  g_object_class_install_property (object_class, PROP_DB_FILE,
    g_param_spec_string ("db-file", "Database file",
                         "Location for media DB file", NULL,
                         G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
snra_media_db_finalize(GObject *object)
{
  SnraMediaDB *media_db = (SnraMediaDB *)(object);

  if (media_db->handle)
    sqlite3_close (media_db->handle);
}

static void
snra_media_db_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  SnraMediaDB *media_db = (SnraMediaDB *)(object);

  switch (prop_id) {
    case PROP_DB_FILE:
      g_free (media_db->db_file);
      media_db->db_file = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
snra_media_db_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  SnraMediaDB *media_db = (SnraMediaDB *)(object);

  switch (prop_id) {
    case PROP_DB_FILE:
      g_value_set_string (value, media_db->db_file);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
media_db_create_tables (SnraMediaDB *media_db)
{
  if (sqlite3_exec (media_db->handle,
      "Create table if not exists paths"
      "(id INTEGER PRIMARY KEY, base_path TEXT)",
      NULL, NULL, NULL) != SQLITE_OK)
    return FALSE;
  if (sqlite3_exec (media_db->handle,
      "Create table if not exists files"
      "(id INTEGER PRIMARY KEY, base_path_id INTEGER, "
      "filename TEXT, timestamp TEXT, "
      "duration INTEGER, is_video INTEGER)", NULL, NULL, NULL) != SQLITE_OK)
    return FALSE;
  if (sqlite3_exec (media_db->handle,
      "Create table if not exists songs"
      "(id INTEGER PRIMARY KEY, media_id INTEGER, "
      "timestamp TEXT, duration INTEGER, "
      "is_video INTEGER)", NULL, NULL, NULL) != SQLITE_OK)
    return FALSE;

  return TRUE;
}

SnraMediaDB *
snra_media_db_new(const char *db_path)
{
  SnraMediaDB *media_db = NULL;

  media_db = g_object_new (SNRA_TYPE_MEDIA_DB, "db-file", db_path, NULL);
  if (media_db == NULL)
    return NULL;

  if (media_db->errored) {
    g_object_unref (media_db);
    media_db = NULL;
  }

  return media_db;
}
