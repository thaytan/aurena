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

#include <glib.h>
#include <glib-unix.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <avahi-glib/glib-malloc.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include <gst/gst.h>
#include <gst/tag/tag.h>

#include <gst/video/videooverlay.h>

#include "client/aur-client.h"

const GdkRGBA BLACK_OPAQUE = {0.0, 0.0, 0.0, 1.0};

typedef struct
{
  AurClient *client;
  GtkLabel *status;
  GtkWidget *video;
  GtkWidget *video_image;
  GtkWidget *master_volume;
  GtkWidget *play_button;
  GtkWidget *play_image;
  GtkWidget *pause_image;
  GtkWidget *next_button;
  GtkWidget *file_chooser;
  GtkWidget *lang_selector;
  GtkListStore *languages;
  GtkWidget *seeker;
  guint seeker_timeout;
  gboolean seeker_grabbed;
  GtkWidget *players_expander;
  GQueue players;
} UIContext;

typedef struct
{
  guint id;
  UIContext *ctx;
  GtkSwitch *enable_switch;
  GtkWidget *volume;
} PlayerContext;

static gboolean
sigint_handler (G_GNUC_UNUSED void * data)
{
  gtk_main_quit ();
  return TRUE;
}

static void
on_eos_msg (G_GNUC_UNUSED UIContext * ctx, G_GNUC_UNUSED GstMessage * msg)
{
  g_print ("EOS reached.\n");
}


static void
player_created_cb (UIContext * ctx, GstElement * player)
{
  GstBus *bus;

  bus = gst_element_get_bus (player);
  g_signal_connect_swapped (bus, "message::eos", G_CALLBACK (on_eos_msg),
      ctx);
  gst_object_unref (bus);

  gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (player),
      GDK_WINDOW_XID (gtk_widget_get_window (ctx->video)));
}

static void
play_toggled_cb (GtkToggleButton * button, UIContext * ctx)
{
  if (gtk_toggle_button_get_active (button)) {
    gtk_button_set_image (GTK_BUTTON (button), ctx->pause_image);
    aur_client_play (ctx->client);
  } else {
    gtk_button_set_image (GTK_BUTTON (button), ctx->play_image);
    aur_client_pause (ctx->client);
  }
}

static void
fullscreen_toggled_cb (GtkToggleButton * button, GtkWindow * window)
{
  if (gtk_toggle_button_get_active (button))
    gtk_window_fullscreen (window);
  else
    gtk_window_unfullscreen (window);
}

static void
next_clicked_cb (UIContext * ctx)
{
  gchar *filename;
  filename =
    gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (ctx->file_chooser));
  aur_client_set_media (ctx->client, filename);
  g_free (filename);
}

static void
position_changed_cb (UIContext * ctx)
{
  if (!ctx->seeker_grabbed) {
    GstClockTime position; 
    position = (GstClockTime) gtk_range_get_value (GTK_RANGE (ctx->seeker));
    aur_client_seek (ctx->client, position);
  }
}

static gboolean
seeker_grabbed_cb (UIContext * ctx)
{
  ctx->seeker_grabbed = TRUE;
  return FALSE;
}

static gboolean
seeker_released_cb (UIContext * ctx)
{
  ctx->seeker_grabbed = FALSE;
  position_changed_cb (ctx);
  return FALSE;
}

static void
client_active_changed_cb (PlayerContext * player)
{
  aur_client_set_player_enabled (player->ctx->client, player->id,
      gtk_switch_get_active (GTK_SWITCH (player->enable_switch)));
}

static void
client_volume_changed_cb (PlayerContext * player, gdouble volume)
{
  aur_client_set_player_volume (player->ctx->client, player->id, volume);
}

static void
language_changed_cb (UIContext * ctx)
{
  GtkTreeIter iter;

  if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (ctx->lang_selector),
        &iter)) {
    gchar *lang;
    gtk_tree_model_get (GTK_TREE_MODEL (ctx->languages), &iter, 1, &lang, -1);
    aur_client_set_language (ctx->client, lang);
    g_free (lang);
  }
}

static void
update_connected_status (UIContext * ctx)
{
  AurClient *client = ctx->client;

  /* Update status message */
  if (!aur_client_is_connected (client)) {
    gtk_label_set_label (ctx->status,
        "Searching for Aurena server on the network ...");
  } else if (!aur_client_is_enabled (client)) {
    gchar *host, *label;
    g_object_get (client, "connected-server", &host, NULL);
    label = g_strdup_printf ("Connnected to %s", host);
    gtk_label_set_label (ctx->status, label);
    g_free (host);
    g_free (label);
  }

  /* Ajust widget sensitivity */
  gtk_widget_set_sensitive (ctx->play_button,
      aur_client_is_connected (client));
  gtk_widget_set_sensitive (ctx->next_button,
      aur_client_is_connected (client));
  gtk_widget_set_sensitive (ctx->file_chooser,
      aur_client_is_connected (client));
  gtk_widget_set_sensitive (ctx->master_volume,
      aur_client_is_connected (client));
}

static void
update_enabled_status (UIContext * ctx)
{
  AurClient *client = ctx->client;

  /* Update audio tracks selection */
  if (aur_client_is_enabled (client)) {
    gtk_widget_set_sensitive (ctx->lang_selector, TRUE);
    /* TODO Fill up audio track list */
  } else {
    gtk_widget_set_sensitive (ctx->lang_selector, FALSE);
  }

  /* Update video box */
  if (aur_client_is_enabled (ctx->client)) {
    gtk_widget_hide (ctx->video_image);
  } else {
    gtk_widget_show (ctx->video_image);
  }
}

static gboolean
update_seeker_position (UIContext * ctx)
{
  AurClient *client = ctx->client;
  GstFormat format = GST_FORMAT_TIME;
  gint64 position;

  if (ctx->seeker_grabbed)
    return TRUE;

  if (gst_element_query_position (client->player, GST_FORMAT_TIME, &position)) {
    if (format != GST_FORMAT_TIME)
      return TRUE;

    g_signal_handlers_block_by_func (ctx->seeker, position_changed_cb, ctx);
    gtk_range_set_value (GTK_RANGE (ctx->seeker), (gdouble) position);
    g_signal_handlers_unblock_by_func (ctx->seeker, position_changed_cb, ctx);
  }

  return TRUE;
}

static void
update_seeker_status (UIContext * ctx)
{
  AurClient *client = ctx->client;

  if (aur_client_is_playing (client)) {
    if (!ctx->seeker_timeout)
      ctx->seeker_timeout = g_timeout_add (250,
          (GSourceFunc) update_seeker_position, ctx);
  } else if (ctx->seeker_timeout) {
    g_source_remove (ctx->seeker_timeout);
    ctx->seeker_timeout = 0;
  }
}

static void
update_paused_status (UIContext *ctx)
{
  AurClient *client = ctx->client;
  
  g_signal_handlers_block_by_func (ctx->play_button, play_toggled_cb, ctx);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ctx->play_button),
      aur_client_is_playing (client));
  g_signal_handlers_unblock_by_func (ctx->play_button, play_toggled_cb, ctx);

  if (aur_client_is_playing (client))
    gtk_button_set_image (GTK_BUTTON (ctx->play_button), ctx->pause_image);
  else
    gtk_button_set_image (GTK_BUTTON (ctx->play_button), ctx->play_image);

  update_seeker_status (ctx);
}

static void
update_volume_status (UIContext *ctx)
{
  AurClient *client = ctx->client;
  gdouble volume;

  g_object_get (client, "volume", &volume, NULL);

  g_signal_handlers_block_by_func (ctx->master_volume,
      aur_client_set_volume, client);
  gtk_scale_button_set_value (GTK_SCALE_BUTTON (ctx->master_volume), volume);
  g_signal_handlers_unblock_by_func (ctx->master_volume,
      aur_client_set_volume, client);
}

static void
update_language_status (UIContext * ctx)
{
  GtkTreeIter iter;
  gint num_audio = 0, cur_audio = 0, size = 0, i;

  g_signal_handlers_block_by_func (ctx->lang_selector, language_changed_cb, ctx);

  if (ctx->client->player)
    g_object_get (ctx->client->player, "n-audio", &num_audio, "current-audio",
        &cur_audio, NULL);

  gtk_list_store_clear (ctx->languages);

  for (i = 0; i < num_audio; i++) {
    GstTagList *tags = NULL;
    gchar *str;
    const gchar *language;

    g_signal_emit_by_name (ctx->client->player, "get-audio-tags", i, &tags);

    if (!tags)
      continue;

    if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str)) {
      gst_tag_list_free (tags);
      language = gst_tag_get_language_name (str);

      gtk_list_store_append (ctx->languages, &iter);
      gtk_list_store_set (ctx->languages, &iter, 0, language, 1, str, 2, i,
          -1);
      size++;
      g_free (str);

      if (i == cur_audio)
        gtk_combo_box_set_active_iter (GTK_COMBO_BOX (ctx->lang_selector), &iter);
    }
  }

  if (num_audio == 0) {
    gtk_list_store_append (ctx->languages, &iter);
    gtk_list_store_set (ctx->languages, &iter, 0, "No Audio", 1, NULL, 2, 0,
        -1);
    gtk_combo_box_set_active_iter (GTK_COMBO_BOX (ctx->lang_selector), &iter);
  } else if (size == 0) {
    gtk_list_store_append (ctx->languages, &iter);
    gtk_list_store_set (ctx->languages, &iter, 0, "Default", 1, NULL, 2,
        cur_audio, -1);
    gtk_combo_box_set_active_iter (GTK_COMBO_BOX (ctx->lang_selector), &iter);
  }

  g_signal_handlers_unblock_by_func (ctx->lang_selector, language_changed_cb, ctx);
}

static void
update_media_uri (UIContext *ctx)
{
  AurClient *client = ctx->client;
  gchar *uri_str, *basename, *host, *label;
  SoupURI *uri;
  GstFormat format = GST_FORMAT_TIME;
  GstClockTime position;
  gint64 duration;

  g_object_get (client,
      "media-uri", &uri_str,
      "position", &position,
      "connected-server", &host,
      NULL);

  uri = soup_uri_new (uri_str);
  basename = g_path_get_basename (soup_uri_get_path (uri));

  label = g_strdup_printf ("Playing track %s from server %s", basename, host);
  gtk_label_set_label (ctx->status, label);

  g_free (uri_str);
  soup_uri_free (uri);
  g_free (basename);
  g_free (host);
  g_free (label);

  if (gst_element_query_duration (client->player, format, &duration)) {
    if (format != GST_FORMAT_TIME)
      duration = 0.0;

    g_signal_handlers_block_by_func (ctx->seeker, position_changed_cb, ctx);
    gtk_range_set_range (GTK_RANGE (ctx->seeker), 0.0, (gdouble) duration);
    gtk_range_set_value (GTK_RANGE (ctx->seeker), (gdouble) position);
    gtk_widget_set_sensitive (ctx->seeker, TRUE);
    g_signal_handlers_unblock_by_func (ctx->seeker, position_changed_cb, ctx);

    update_seeker_status (ctx);
  }

  update_language_status (ctx);
}

static gchar *
get_ui_filepath_from_basename (const gchar *basename)
{
  const gchar * const * dirs;
  gchar *filepath = NULL;
  gint i;

  dirs = g_get_system_data_dirs ();

  for (i = 0; dirs[i] != NULL; i++) {
    GFile *file;

    filepath = g_build_filename (dirs[i], "aurena", basename, NULL);
    file = g_file_new_for_path (filepath);
    if (g_file_query_exists (file, NULL)) {
      g_object_unref (file);
      break;
    }

    g_object_unref (file);
    g_free (filepath);
    filepath = NULL;
  }

  if (!filepath) {
    GFile *file;

    filepath = g_build_filename (g_get_current_dir (), "data", basename, NULL);
    file = g_file_new_for_path (filepath);
    if (!g_file_query_exists (file, NULL)) {
      g_free (filepath);
      filepath = NULL;
    }
    g_object_unref (file);
  }

  return filepath;
}

static void
update_players_status (UIContext * ctx)
{
  const GArray *infos;
  GtkWidget *vbox;
  gchar *label, *filepath;
  GError *error = NULL;
  gsize i;

  while (!g_queue_is_empty (&ctx->players)) {
    PlayerContext *player = g_queue_pop_head (&ctx->players);
    g_slice_free (PlayerContext, player);
  }

  if (gtk_bin_get_child (GTK_BIN (ctx->players_expander)))
    gtk_container_remove (GTK_CONTAINER (ctx->players_expander),
        gtk_bin_get_child (GTK_BIN (ctx->players_expander)));

  infos = aur_client_get_player_info (ctx->client);
  if (!infos || !infos->len) {
    gtk_expander_set_expanded (GTK_EXPANDER (ctx->players_expander), FALSE);
    gtk_expander_set_label (GTK_EXPANDER (ctx->players_expander), "No player connected");
    gtk_widget_set_sensitive (ctx->players_expander, FALSE);
    return;
  }

  label = g_strdup_printf ("%i player%s connected", infos->len,
      infos->len > 1 ? "s" : "");
  gtk_expander_set_label (GTK_EXPANDER (ctx->players_expander), label);
  g_free (label);
  gtk_widget_set_sensitive (ctx->players_expander, TRUE);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add (GTK_CONTAINER (ctx->players_expander), vbox);
  
  filepath = get_ui_filepath_from_basename ("simple-controller-client.ui");
  if (!filepath) {
    g_error ("Could not find simple-controller-client.ui, please fix XDG_DATA_DIRS");
    return;
  } 

  for (i = 0; i < infos->len; i++) {
    GtkBuilder *builder;
    GtkLabel *label;
    GtkAdjustment *adjustment;
    PlayerContext *player;
    AurPlayerInfo *info = &((AurPlayerInfo *) infos->data)[i];

    builder = gtk_builder_new ();
    if (!gtk_builder_add_from_file (builder, filepath, &error)) {
      g_error ("Failed to load simple-controller-client.ui: %s",
          error->message);
      g_object_unref (builder);
      goto failed;
    }

    player = g_slice_new0 (PlayerContext);
    player->id = info->id;
    player->ctx = ctx;
    player->enable_switch =
      GTK_SWITCH (gtk_builder_get_object (builder, "clientSwitch"));
    player->volume =
      GTK_WIDGET (gtk_builder_get_object (builder, "clientVolume"));

    gtk_switch_set_active (player->enable_switch, info->enabled);
    label = GTK_LABEL (gtk_builder_get_object (builder, "clientLabel"));
    gtk_label_set_label (label, info->host);
    g_object_get (player->volume, "adjustment", &adjustment, NULL);
    gtk_adjustment_configure (adjustment, info->volume, 0.0, 1.5, 0.03, 0.0, 0.0);
    g_object_unref (adjustment);

    gtk_box_pack_start (GTK_BOX (vbox), 
        GTK_WIDGET (gtk_builder_get_object (builder, "clientBox")),
        FALSE, TRUE, 4);
    g_object_unref (builder);

    g_signal_connect_swapped (player->enable_switch, "notify::active",
        G_CALLBACK (client_active_changed_cb), player);
    g_signal_connect_swapped (player->volume, "value-changed",
        G_CALLBACK (client_volume_changed_cb), player);

    g_queue_push_tail (&ctx->players, player);
  }

  gtk_widget_show_all (vbox);

failed:
  g_free (filepath);
}

static void
update_player_enabled_status (UIContext * ctx, guint id, gboolean enabled)
{
  GList *l;

  for (l = ctx->players.head; l; l = g_list_next (l)) {
    PlayerContext *player = l->data;
    if (player->id == id) {
      g_signal_handlers_block_by_func (player->enable_switch,
          client_active_changed_cb, player);
      gtk_switch_set_active (GTK_SWITCH (player->enable_switch), enabled);
      g_signal_handlers_unblock_by_func (player->enable_switch,
          client_active_changed_cb, player);
      return;
    }
  }
}

static void
update_player_volume_status (UIContext * ctx, guint id, gdouble volume)
{
  GList *l;

  for (l = ctx->players.head; l; l = g_list_next (l)) {
    PlayerContext *player = l->data;
    if (player->id == id) {
      g_signal_handlers_block_by_func (player->volume, client_volume_changed_cb,
          player);
      gtk_scale_button_set_value (GTK_SCALE_BUTTON (player->volume), volume);
      g_signal_handlers_unblock_by_func (player->volume, client_volume_changed_cb,
          player);
      return;
    }
  }
}

gint
main (gint argc, gchar *argv[])
{
  gint ret = 1;

  gchar *filepath;
  GError *error = NULL;
  const gchar *server = NULL;
  GtkBuilder *builder = NULL;
  
  UIContext ctx;
  GtkWidget *window;
  GtkWidget *fullscreen_button;
  GtkAdjustment *adjustment;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  if (argc > 1) {
    /* Connect directly to the requested server, no avahi */
    server = argv[1];
  }

  /* Create Aurena client */
  avahi_set_allocator (avahi_glib_allocator ());
  ctx.client = aur_client_new (NULL, server,
      AUR_CLIENT_PLAYER | AUR_CLIENT_CONTROLLER);
  if (ctx.client == NULL)
    goto fail;

  /* Create UI */
  g_unix_signal_add (SIGINT, sigint_handler, NULL);
  builder = gtk_builder_new ();
  filepath = get_ui_filepath_from_basename ("simple-controller.ui");

  if (!filepath) {
    g_error ("Could not find simple-controller.ui, please set XDG_DATA_DIRS.");
    goto fail;
  }

  if (!gtk_builder_add_from_file (builder, filepath, &error)) {
    g_error ("Failed to open %s: %s", filepath, error->message);
    g_error_free (error);
    goto fail;
  }
  g_free (filepath);

  window = GTK_WIDGET (gtk_builder_get_object (builder, "aurenaController"));
  fullscreen_button =
      GTK_WIDGET (gtk_builder_get_object (builder, "fullscreenButton"));
  ctx.status = GTK_LABEL (gtk_builder_get_object (builder, "statusLabel"));
  ctx.video = GTK_WIDGET (gtk_builder_get_object (builder, "videoBox"));
  ctx.video_image = GTK_WIDGET (gtk_builder_get_object (builder, "videoImage"));
  gtk_widget_override_background_color (ctx.video, 0, &BLACK_OPAQUE); 
  ctx.master_volume =
    GTK_WIDGET (gtk_builder_get_object (builder, "volumeButton"));
  g_object_get (ctx.master_volume, "adjustment", &adjustment, NULL);
  gtk_adjustment_configure (adjustment, 0.0, 0.0, 1.5, 0.03, 0.0, 0.0);
  g_object_unref (adjustment);
  ctx.play_button = GTK_WIDGET (gtk_builder_get_object (builder, "playButton"));
  ctx.play_image = GTK_WIDGET (gtk_builder_get_object (builder, "playImage"));
  ctx.pause_image = GTK_WIDGET (gtk_builder_get_object (builder, "pauseImage"));
  ctx.next_button = GTK_WIDGET (gtk_builder_get_object (builder, "nextButton"));
  ctx.file_chooser =
    GTK_WIDGET (gtk_builder_get_object (builder, "fileChooser"));
  ctx.lang_selector =
    GTK_WIDGET (gtk_builder_get_object (builder, "languageSelector"));
  ctx.languages = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_INT);
  gtk_combo_box_set_model (GTK_COMBO_BOX (ctx.lang_selector),
      GTK_TREE_MODEL (ctx.languages));
  ctx.seeker = GTK_WIDGET (gtk_builder_get_object (builder, "seekerScale"));
  gtk_range_set_increments (GTK_RANGE (ctx.seeker), 2.0l * (gdouble) GST_SECOND,
      10.0l * (gdouble) GST_SECOND);
  ctx.seeker_timeout = 0;
  ctx.seeker_grabbed = FALSE;
  ctx.players_expander =
    GTK_WIDGET (gtk_builder_get_object (builder, "playersExpander"));
  g_queue_init (&ctx.players);

  /* Actions */
  g_signal_connect (window, "destroy", G_CALLBACK (gtk_main_quit), NULL);
  g_signal_connect (fullscreen_button, "toggled",
      G_CALLBACK (fullscreen_toggled_cb), window);
  g_signal_connect_swapped (ctx.master_volume, "value-changed",
      G_CALLBACK (aur_client_set_volume), ctx.client);
  g_signal_connect (ctx.play_button, "toggled", G_CALLBACK (play_toggled_cb),
      &ctx);
  g_signal_connect_swapped (ctx.next_button, "clicked",
      G_CALLBACK (next_clicked_cb), &ctx);
  g_signal_connect_swapped (ctx.seeker, "value-changed",
      G_CALLBACK (position_changed_cb), &ctx);
  g_signal_connect_swapped (ctx.seeker, "button-press-event",
      G_CALLBACK (seeker_grabbed_cb), &ctx);
  g_signal_connect_swapped (ctx.seeker, "button-release-event",
      G_CALLBACK (seeker_released_cb), &ctx);
  g_signal_connect_swapped (ctx.lang_selector, "changed",
      G_CALLBACK (language_changed_cb), &ctx);

  /* Updates */
  g_signal_connect_swapped (ctx.client, "notify::connected-server",
      G_CALLBACK (update_connected_status), &ctx);
  g_signal_connect_swapped (ctx.client, "notify::connected-server",
      G_CALLBACK (update_enabled_status), &ctx);
  g_signal_connect_swapped (ctx.client, "player-created",
      G_CALLBACK (player_created_cb), &ctx);
  g_signal_connect_swapped (ctx.client, "notify::enabled",
      G_CALLBACK (update_enabled_status), &ctx);
  g_signal_connect_swapped (ctx.client, "notify::paused",
      G_CALLBACK (update_paused_status), &ctx);
  g_signal_connect_swapped (ctx.client, "notify::volume",
      G_CALLBACK (update_volume_status), &ctx);
  g_signal_connect_swapped (ctx.client, "notify::media-uri",
      G_CALLBACK (update_media_uri), &ctx);
  g_signal_connect_swapped (ctx.client, "player-info-changed",
      G_CALLBACK (update_players_status), &ctx);
  g_signal_connect_swapped (ctx.client, "client-setting-changed",
      G_CALLBACK (update_player_enabled_status), &ctx);
  g_signal_connect_swapped (ctx.client, "client-setting-changed",
      G_CALLBACK (update_player_volume_status), &ctx);

  update_connected_status (&ctx);
  update_enabled_status (&ctx);
  update_paused_status (&ctx);
  update_players_status (&ctx);
  update_language_status (&ctx);

  gtk_widget_show_all (window);
  gtk_main ();

  ret = 0;
fail:
  if (ctx.client)
    g_object_unref (ctx.client);
  if (builder)
    g_object_unref (builder);
  return ret;
}
