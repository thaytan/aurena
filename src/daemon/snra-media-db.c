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

struct _SnraMediaDBPriv
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
snra_media_db_init (SnraMediaDB *media_db)
{
  media_db->priv = G_TYPE_INSTANCE_GET_PRIVATE (media_db,
      SNRA_TYPE_MEDIA_DB, SnraMediaDBPriv);
}

static void
snra_media_db_constructed (G_GNUC_UNUSED GObject *object)
{
  SnraMediaDB *media_db = (SnraMediaDB *)(object);
  sqlite3 *handle = NULL;

  if (G_OBJECT_CLASS (snra_media_db_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (snra_media_db_parent_class)->constructed (object);

  g_mkdir_with_parents (g_path_get_dirname (media_db->priv->db_file), 0755); 

  if (sqlite3_open (media_db->priv->db_file, &handle) != SQLITE_OK) {
    g_warning ("Could not open media DB %s\n", media_db->priv->db_file);
    media_db->priv->errored = TRUE;
  }

  media_db->priv->handle = handle;

  if (!media_db_create_tables(media_db))
    media_db->priv->errored = TRUE;
  g_print ("media DB ready at %s\n", media_db->priv->db_file);
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
  g_type_class_add_private (object_class, sizeof (SnraMediaDBPriv));
}

static void
snra_media_db_finalize(GObject *object)
{
  SnraMediaDB *media_db = (SnraMediaDB *)(object);

  if (media_db->priv->handle)
    sqlite3_close (media_db->priv->handle);
}

static void
snra_media_db_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  SnraMediaDB *media_db = (SnraMediaDB *)(object);

  switch (prop_id) {
    case PROP_DB_FILE:
      g_free (media_db->priv->db_file);
      media_db->priv->db_file = g_value_dup_string (value);
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
      g_value_set_string (value, media_db->priv->db_file);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
media_db_create_tables (SnraMediaDB *media_db)
{
  sqlite3 *handle = media_db->priv->handle;
  if (sqlite3_exec (handle,
      "Create table if not exists paths"
      "(id INTEGER PRIMARY KEY, base_path TEXT)",
      NULL, NULL, NULL) != SQLITE_OK)
    return FALSE;
  if (sqlite3_exec (handle,
      "Create table if not exists files"
      "(id INTEGER PRIMARY KEY, base_path_id INTEGER, "
      "filename TEXT, timestamp TEXT, "
      "duration INTEGER, is_video INTEGER)", NULL, NULL, NULL) != SQLITE_OK)
    return FALSE;
  if (sqlite3_exec (handle,
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

  if (media_db->priv->errored) {
    g_object_unref (media_db);
    media_db = NULL;
  }

  return media_db;
}

static guint64
snra_media_path_to_id(SnraMediaDB *media_db, const gchar *path)
{
  sqlite3_stmt *stmt = NULL;
  sqlite3_stmt *insert_stmt = NULL;
  guint64 path_id = (guint64)(-1);
  sqlite3 *handle = media_db->priv->handle;

  if (sqlite3_prepare (handle,
      "select id from paths where base_path=?", -1, &stmt, NULL) != SQLITE_OK)
    goto done;

  if (sqlite3_bind_text (stmt, 1, g_strdup (path), -1, g_free) != SQLITE_OK)
    goto done;

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    path_id = sqlite3_column_int64 (stmt, 0);
    goto done;
  }

  /* Row not found, insert it */
  if (sqlite3_prepare (handle,
      "insert into paths (base_path) VALUES (?)",
      -1, &insert_stmt, NULL) != SQLITE_OK)
    goto done;

  if (sqlite3_bind_text (insert_stmt, 1,
      g_strdup (path), -1, g_free) != SQLITE_OK)
    goto done;

  if (sqlite3_step(insert_stmt) == SQLITE_DONE) {
    path_id = sqlite3_last_insert_rowid (handle);
    goto done;
  } 

done:
  if (insert_stmt)
    sqlite3_finalize (insert_stmt);
  if (stmt)
    sqlite3_finalize (stmt);
  return path_id;
}

static guint64
snra_media_file_to_id(SnraMediaDB *media_db, guint64 path_id, const gchar *file)
{
  sqlite3_stmt *stmt = NULL;
  sqlite3_stmt *insert_stmt = NULL;
  guint64 file_id = (guint64)(-1);
  sqlite3 *handle = media_db->priv->handle;

  if (sqlite3_prepare (handle,
      "select id from files where base_path_id=? and filename=?",
      -1, &stmt, NULL) != SQLITE_OK)
    goto done;

  if (sqlite3_bind_int64 (stmt, 1, path_id) != SQLITE_OK)
    goto done;
  if (sqlite3_bind_text (stmt, 2, g_strdup (file), -1, g_free) != SQLITE_OK)
    goto done;

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    file_id = sqlite3_column_int64 (stmt, 0);
    goto done;
  }

  /* Row not found, insert it */
  if (sqlite3_prepare (handle,
      "insert into files (base_path_id, filename) VALUES (?,?)",
      -1, &insert_stmt, NULL) != SQLITE_OK)
    goto done;

  if (sqlite3_bind_int64 (insert_stmt, 1, path_id) != SQLITE_OK)
    goto done;
  if (sqlite3_bind_text (insert_stmt, 2,
      g_strdup (file), -1, g_free) != SQLITE_OK)
    goto done;

  if (sqlite3_step(insert_stmt) == SQLITE_DONE) {
    file_id = sqlite3_last_insert_rowid (handle);
    goto done;
  }

done:
  if (insert_stmt)
    sqlite3_finalize (insert_stmt);
  if (stmt)
    sqlite3_finalize (stmt);
  return file_id;
}

void
snra_media_db_add_file (SnraMediaDB *media_db, const gchar *filename)
{
  gchar *path = g_path_get_dirname(filename);
  gchar *file = g_path_get_basename(filename);
  guint64 path_id;
  
  path_id = snra_media_path_to_id(media_db, path);
  snra_media_file_to_id(media_db, path_id, file);

  //g_print ("File %s has id %" G_GUINT64_FORMAT "\n",
    //filename, file_id);

  g_free (path);
  g_free (file);
}

guint
snra_media_db_get_file_count (SnraMediaDB *media_db)
{
  sqlite3_stmt *stmt = NULL;
  gint count = -1;
  sqlite3 *handle = media_db->priv->handle;

  if (sqlite3_prepare (handle,
      "select count(*) from files", -1, &stmt, NULL) != SQLITE_OK)
    goto done;

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int (stmt, 0);
    goto done;
  }
 
done:
  g_print ("Got count %d\n", count);
  return count;
}

gchar *
snra_media_db_get_file_by_id (SnraMediaDB *media_db, guint id)
{
  sqlite3_stmt *stmt = NULL;
  sqlite3 *handle = media_db->priv->handle;
  gchar *ret_path = NULL;

  if (id < 1)
    goto done;

  if (sqlite3_prepare (handle,
      "select base_path, filename from paths, files where paths.id = files.base_path_id "
      "limit 1 offset ?",
       -1, &stmt, NULL) != SQLITE_OK)
    goto done;

  if (sqlite3_bind_int64 (stmt, 1, id-1) != SQLITE_OK)
    goto done;

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const gchar *base_path, *filename;
    base_path = (gchar *)sqlite3_column_text (stmt, 0);
    filename = (gchar *)sqlite3_column_text (stmt, 1);
    ret_path = g_build_filename (base_path, filename, NULL);
    goto done;
  }

done:
  return ret_path;
}
