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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>

#include "aur-config.h"

G_DEFINE_TYPE (AurConfig, aur_config, G_TYPE_OBJECT);

enum
{
  PROP_0,
  PROP_CONFIG_FILE,
  PROP_PORT,
  PROP_RTSP_PORT,
  PROP_DATABASE,
  PROP_PLAYLIST,
  PROP_LAST
};

static void aur_config_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void aur_config_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void aur_config_finalize (GObject * object);
static void aur_config_dispose (GObject * object);

static gchar *
get_default_db_location ()
{
  return g_build_filename (g_get_user_data_dir (), "aurena", "aurena.db", NULL);
}

static gchar *
get_default_playlist_location ()
{
  return g_build_filename (g_get_user_config_dir (),
      "aurena", "playlist.txt", NULL);
}

static gchar *
get_default_config_location ()
{
  return g_build_filename (g_get_user_config_dir (),
      "aurena", "config.txt", NULL);
}

static void
aur_config_init (AurConfig * config)
{
  config->config_file = get_default_config_location ();
  config->aur_port = 5457;
  config->rtsp_port = 5458;
  config->database_location = get_default_db_location ();
  config->playlist_location = get_default_playlist_location ();
}

static void
try_read_int (GKeyFile * kf, const gchar * group, const gchar * key,
    gint * dest)
{
  GError *error = NULL;
  gint tmp = g_key_file_get_integer (kf, group, key, &error);

  if (error) {
    g_error_free (error);
    return;
  }
  *dest = tmp;
}

static void
try_read_string (GKeyFile * kf, const gchar * group, const gchar * key,
    gchar ** dest)
{
  GError *error = NULL;
  gchar *tmp = g_key_file_get_string (kf, group, key, &error);

  if (error) {
    g_error_free (error);
    return;
  }
  g_free (*dest);
  *dest = tmp;
}

static void
make_abs_path (gchar ** dest, gchar * rel)
{
  if (!g_path_is_absolute (*dest)) {
    /* Make path absolute, relative to the config file */
    gchar *dir = g_path_get_dirname (rel);
    gchar *abs_location = g_build_filename (dir, *dest, NULL);
    g_free (dir);
    g_free (*dest);
    *dest = abs_location;
  }
}

static void
load_config (G_GNUC_UNUSED AurConfig * config)
{
  /* Read in config_file and split out pieces to the vars */
  GKeyFile *kf = g_key_file_new ();
  if (kf == NULL)
    goto fail;

  if (!g_key_file_load_from_file (kf, config->config_file,
          G_KEY_FILE_KEEP_COMMENTS, NULL))
    goto fail;

  try_read_int (kf, "server", "port", &config->aur_port);
  try_read_int (kf, "server", "rtsp-port", &config->aur_port);
  try_read_string (kf, "server", "database", &config->database_location);
  try_read_string (kf, "server", "playlist", &config->playlist_location);
  make_abs_path (&config->database_location, config->config_file);
  make_abs_path (&config->playlist_location, config->config_file);

  g_key_file_free (kf);
  return;

fail:
  if (kf)
    g_key_file_free (kf);
  g_warning ("Failed to read config file %s", config->config_file);
}

static void
aur_config_constructed (GObject * object)
{
  AurConfig *config = (AurConfig *) (object);

  if (G_OBJECT_CLASS (aur_config_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (aur_config_parent_class)->constructed (object);

  load_config (config);
}

static void
aur_config_class_init (AurConfigClass * config_class)
{
  GObjectClass *gobject_class = (GObjectClass *) (config_class);
  gchar *location;

  gobject_class->constructed = aur_config_constructed;

  gobject_class->dispose = aur_config_dispose;
  gobject_class->finalize = aur_config_finalize;
  gobject_class->set_property = aur_config_set_property;
  gobject_class->get_property = aur_config_get_property;

  location = get_default_config_location ();
  g_object_class_install_property (gobject_class, PROP_CONFIG_FILE,
      g_param_spec_string ("config-file", "configuration file",
          "Location of the configuration file",
          location, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_free (location);

  g_object_class_install_property (gobject_class, PROP_PORT,
      g_param_spec_int ("aur-port", "Aurena port",
          "port for Aurena service", 1, 65535, 5457, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_RTSP_PORT,
      g_param_spec_int ("rtsp-port", "RTSP port",
          "port for RTSP server", 1, 65535, 5458, G_PARAM_READWRITE));

  location = get_default_db_location ();
  g_object_class_install_property (gobject_class, PROP_DATABASE,
      g_param_spec_string ("database", "database",
          "Location of the media database file", location, G_PARAM_READWRITE));
  g_free (location);

  location = get_default_playlist_location ();
  g_object_class_install_property (gobject_class, PROP_PLAYLIST,
      g_param_spec_string ("playlist", "playlist",
          "Location of the media playlist file", location, G_PARAM_READWRITE));
  g_free (location);

}

static void
aur_config_finalize (GObject * object)
{
  AurConfig *config = (AurConfig *) (object);

  g_free (config->config_file);
  g_free (config->database_location);
  g_free (config->playlist_location);

  G_OBJECT_CLASS (aur_config_parent_class)->finalize (object);
}

static void
aur_config_dispose (GObject * object)
{
  G_OBJECT_CLASS (aur_config_parent_class)->dispose (object);
}

static void
aur_config_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  AurConfig *config = (AurConfig *) (object);

  switch (prop_id) {
    case PROP_CONFIG_FILE:
      if (config->config_file)
        g_free (config->config_file);
      config->config_file = g_value_dup_string (value);
      if (config->config_file == NULL)
        config->config_file = get_default_config_location ();
      break;
    case PROP_PORT:
      config->aur_port = g_value_get_int (value);
      break;
    case PROP_RTSP_PORT:
      config->rtsp_port = g_value_get_int (value);
      break;
    case PROP_DATABASE:
      if (config->database_location)
        g_free (config->database_location);
      config->database_location = g_value_dup_string (value);
      if (config->database_location == NULL)
        config->database_location = get_default_db_location ();
      break;
    case PROP_PLAYLIST:
      if (config->playlist_location)
        g_free (config->playlist_location);
      config->playlist_location = g_value_dup_string (value);
      if (config->playlist_location == NULL)
        config->playlist_location = get_default_playlist_location ();
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
aur_config_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  AurConfig *config = (AurConfig *) (object);

  switch (prop_id) {
    case PROP_CONFIG_FILE:
      g_value_set_string (value, config->config_file);
      break;
    case PROP_PORT:
      g_value_set_int (value, config->aur_port);
      break;
    case PROP_RTSP_PORT:
      g_value_set_int (value, config->rtsp_port);
      break;
    case PROP_DATABASE:
      g_value_set_string (value, config->database_location);
      break;
    case PROP_PLAYLIST:
      g_value_set_string (value, config->playlist_location);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

AurConfig *
aur_config_new (const gchar * config_file)
{
  /* FIXME: return NULL if config file loading failed? */
  return g_object_new (AUR_TYPE_CONFIG, "config-file", config_file, NULL);
}
