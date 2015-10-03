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

/*
 * Aurena Client is the central object which:
 *   creates the network clock
 *   Establishes libsoup session
 *   Creates RTSP sessions as needed
 *   Distributes the network clock and base time to clients
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#ifdef ANDROID
#include <android/log.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#if !GLIB_CHECK_VERSION(2,22,0)
/* GResolver not available */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

#include "src/common/aur-json.h"
#include "aur-client.h"

GST_DEBUG_CATEGORY_STATIC (client_debug);
#define GST_CAT_DEFAULT client_debug

static void
_do_init ()
{
  GST_DEBUG_CATEGORY_INIT (client_debug, "aurena/client", 0,
      "Aurena Client debug");
}

G_DEFINE_TYPE_WITH_CODE (AurClient, aur_client, G_TYPE_OBJECT, _do_init ());

#if defined(ANDROID) && defined(NDK_DEBUG)
#define g_print(...) __android_log_print(ANDROID_LOG_ERROR, "aurena", __VA_ARGS__)
#endif

enum
{
  PROP_0,
  PROP_SERVER_HOST,
  PROP_FLAGS,
  PROP_PAUSED,
  PROP_BASE_TIME,
  PROP_POSITION,
  PROP_MEDIA_URI,
  PROP_VOLUME,
  PROP_CONNECTED_SERVER,
  PROP_ENABLED,
  PROP_LANGUAGE,
  PROP_ASYNC_MAIN_CONTEXT,
  PROP_LAST
};

enum
{
  SIGNAL_PLAYER_CREATED,
  SIGNAL_CLIENT_VOLUME_CHANGED,
  SIGNAL_CLIENT_SETTING_CHANGED,
  SIGNAL_PLAYER_INFO_CHANGED,
  NUM_SIGNALS
};

int signals[NUM_SIGNALS];

static void aur_client_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void aur_client_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void aur_client_finalize (GObject * object);
static void aur_client_dispose (GObject * object);

static void search_for_server (AurClient * client);
static void connect_to_server (AurClient * client, const gchar * server,
    int port);
static void construct_player (AurClient * client);

static GSource *
aur_client_new_timeout (AurClient * client, guint interval, GSourceFunc cb)
{
  GSource *s = g_timeout_source_new_seconds (interval);
  g_source_set_callback (s, (GSourceFunc) cb, client, NULL);
  g_source_attach (s, client->context);
  return s;
}

static void
free_player_info (GArray * player_info)
{
  gsize i;

  if (!player_info)
    return;

  for (i = 0; i < player_info->len; i++) {
    AurPlayerInfo *info = &g_array_index (player_info, AurPlayerInfo, i);
    g_free (info->host);
  }

  g_array_free (player_info, TRUE);
}

static gboolean
try_reconnect (AurClient * client)
{
  if (client->recon_timeout) {
    g_source_destroy (client->recon_timeout);
    client->recon_timeout = NULL;
  }

  GST_LOG_OBJECT (client, "Entering reconnect. server_host %s",
      client->server_host);

  if (client->server_host)
    connect_to_server (client, client->server_host, client->server_port);
  else
    search_for_server (client);

  return FALSE;
}

static AurClientFlags
get_flag_from_msg (SoupMessage * msg)
{
  AurClientFlags flag;
  SoupURI *uri = soup_message_get_uri (msg);

  if (g_str_equal (soup_uri_get_path (uri), "/client/control_events"))
    flag = AUR_CLIENT_CONTROLLER;
  else
    flag = AUR_CLIENT_PLAYER;

  return flag;
}

static gboolean
conn_idle_timeout (AurClient * client)
{
  client->idle_timeout = 0;

  if (client->msg) {
    g_print ("Connection timed out\n");
    soup_session_cancel_message (client->soup, client->msg, 200);
  }

  return FALSE;
}

static void
handle_connection_closed_cb (G_GNUC_UNUSED SoupSession * session,
    SoupMessage * msg, AurClient * client)
{
  AurClientFlags flag = get_flag_from_msg (msg);

  GST_LOG_OBJECT (client, "lost connection -flag %d", flag);

  client->connecting &= ~flag;

  if (client->idle_timeout) {
    g_source_destroy (client->idle_timeout);
    client->idle_timeout = NULL;
  }

  if (msg->status_code == SOUP_STATUS_CANCELLED)
    return;

  if (client->was_connected & flag) {
    g_print ("%s disconnected from server. Reason %s status %d\n",
        flag == AUR_CLIENT_PLAYER ? "Player" : "Controller",
        msg->reason_phrase, msg->status_code);
  }
  client->was_connected &= ~flag;

  if (flag == AUR_CLIENT_PLAYER) {
    if (client->player)
      gst_element_set_state (client->player, GST_STATE_READY);
    if (client->record_pipe)
      gst_element_set_state (client->record_pipe, GST_STATE_READY);
  }

  if (client->player_info) {
    free_player_info (client->player_info);
    client->player_info = NULL;
    g_signal_emit (client, signals[SIGNAL_PLAYER_INFO_CHANGED], 0);
  }

  if (!client->was_connected) {
    g_free (client->connected_server);
    client->connected_server = NULL;
    client->connected_port = 0;
    client->paused = TRUE;
    client->enabled = FALSE;
    g_object_notify (G_OBJECT (client), "paused");
    g_object_notify (G_OBJECT (client), "enabled");
    g_object_notify (G_OBJECT (client), "connected-server");
  }

  if (client->recon_timeout == NULL) {
    GST_LOG_OBJECT (client, "Scheduling reconnect attempt in 1 second");
    client->recon_timeout =
        aur_client_new_timeout (client, 1, (GSourceFunc) try_reconnect);
  }
}

static void
handle_player_enrol_message (AurClient * client, GstStructure * s)
{
  int clock_port;
  gint64 tmp;
  GstClockTime cur_time;
  gchar *server_ip_str = NULL;
  gdouble new_vol;

  if (!aur_json_structure_get_int (s, "clock-port", &clock_port))
    return;                     /* Invalid message */

  if (!aur_json_structure_get_int64 (s, "current-time", &tmp))
    return;                     /* Invalid message */
  cur_time = (GstClockTime) (tmp);

  if (client->player == NULL)
    construct_player (client);

  if (aur_json_structure_get_double (s, "volume-level", &new_vol)) {
    if (client->player == NULL)
      construct_player (client);

    if (client->player) {
      //g_print ("New volume %g\n", new_vol);
      g_object_set (G_OBJECT (client->player), "volume", new_vol,
          "mute", (gboolean) (new_vol == 0.0), NULL);
    }
  }

  aur_json_structure_get_boolean (s, "enabled", &client->enabled);
  aur_json_structure_get_boolean (s, "paused", &client->paused);

#if GLIB_CHECK_VERSION(2,22,0)
  {
    GResolver *resolver = g_resolver_get_default ();
    GList *names;

    if (resolver == NULL)
      return;

    names =
        g_resolver_lookup_by_name (resolver, client->connected_server, NULL,
        NULL);
    if (names) {
      server_ip_str = g_inet_address_to_string ((GInetAddress *) (names->data));
      g_resolver_free_addresses (names);
    }
    g_object_unref (resolver);
  }
#else
  {
    struct addrinfo *names = NULL;
    if (getaddrinfo (client->connected_server, NULL, NULL, &names))
      return;
    if (names) {
      char hbuf[NI_MAXHOST];
      if (getnameinfo (names->ai_addr, names->ai_addrlen,
              hbuf, sizeof (hbuf), NULL, 0, NI_NUMERICHOST) == 0) {
        server_ip_str = g_strdup (hbuf);
      }
      freeaddrinfo (names);
    }
  }
#endif
  if (server_ip_str) {
    GST_LOG_OBJECT (client,
        "Creating net clock at %s:%d time %" GST_TIME_FORMAT "\n",
        server_ip_str, clock_port, GST_TIME_ARGS (cur_time));
    if (client->net_clock)
      gst_object_unref (client->net_clock);
    client->net_clock = gst_net_client_clock_new ("net_clock", server_ip_str,
        clock_port, cur_time);
    g_free (server_ip_str);
  }

  g_object_notify (G_OBJECT (client), "enabled");
  g_object_notify (G_OBJECT (client), "paused");
}

static void
on_error_msg (G_GNUC_UNUSED GstBus * bus, GstMessage * msg,
    G_GNUC_UNUSED AurClient * client)
{
  GError *err;
  gchar *dbg_info = NULL;

  gst_message_parse_error (msg, &err, &dbg_info);
  g_printerr ("ERROR from element %s: %s\n",
      GST_OBJECT_NAME (msg->src), err->message);
  g_printerr ("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");
  g_error_free (err);
  g_free (dbg_info);
}

static void
construct_player (AurClient * client)
{
  GstBus *bus;
  guint flags;

  GST_DEBUG ("Constructing playbin");
  client->player = gst_element_factory_make ("playbin", NULL);

  if (client->player == NULL) {
    g_warning ("Failed to construct playbin");
    return;
  }

  g_object_get (client->player, "flags", &flags, NULL);
  /* Disable subtitles for now */
  flags &= ~0x00000004;
  g_object_set (client->player, "flags", flags, NULL);

  bus = gst_element_get_bus (GST_ELEMENT (client->player));

#if 0
  gst_bus_add_signal_watch (bus);
#else
  {
    GSource *bus_source;
    bus_source = gst_bus_create_watch (bus);
    g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func,
        NULL, NULL);
    g_source_attach (bus_source, client->context);
    g_source_unref (bus_source);
  }
#endif

  g_signal_connect (bus, "message::error", (GCallback) (on_error_msg), client);
  gst_object_unref (bus);

  gst_element_set_state (client->player, GST_STATE_READY);

  g_signal_emit (client, signals[SIGNAL_PLAYER_CREATED], 0, client->player);
}

static void
set_language (AurClient * client)
{
  gint num_audio, cur_audio, i;

  g_object_get (client->player, "n-audio", &num_audio, "current-audio",
      &cur_audio, NULL);

  for (i = 0; i < num_audio; i++) {
    GstTagList *tags;
    gchar *str = NULL;
    gboolean found = FALSE;

    g_signal_emit_by_name (client->player, "get-audio-tags", i, &tags);
    if (!tags)
      continue;

    if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str))
      found = g_str_equal (client->language, str);

    gst_tag_list_unref (tags);
    g_free (str);

    if (found) {
      g_object_set (client->player, "current-audio", i, NULL);
      break;
    }
  }
}

static void
set_media (AurClient * client)
{
  if (client->player == NULL) {
    construct_player (client);
    if (client->player == NULL)
      return;
  }

  gst_element_set_state (client->player, GST_STATE_READY);

  GST_INFO_OBJECT (client,
      "Setting media URI %s base_time %" GST_TIME_FORMAT " position %"
      GST_TIME_FORMAT " paused %i", client->uri,
      GST_TIME_ARGS (client->base_time), GST_TIME_ARGS (client->position),
      client->paused);
  g_object_set (client->player, "uri", client->uri, NULL);

  gst_element_set_start_time (client->player, GST_CLOCK_TIME_NONE);
  gst_pipeline_use_clock (GST_PIPELINE (client->player), client->net_clock);

  /* Do the preroll */
  gst_element_set_state (client->player, GST_STATE_PAUSED);
  gst_element_get_state (client->player, NULL, NULL, GST_CLOCK_TIME_NONE);

  /* Compensate preroll time if playing */
  if (!client->paused) {
    GstClockTime now = gst_clock_get_time (client->net_clock);
    if (now > (client->base_time + client->position))
      client->position = now - client->base_time;
  }

  /* If position is off by more than 0.5 sec, seek to that position
   * (otherwise, just let the player skip) */
  if (client->position > GST_SECOND / 2) {
    /* FIXME Query duration, so we don't seek after EOS */
    if (!gst_element_seek_simple (client->player, GST_FORMAT_TIME,
            GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, client->position)) {
      g_warning ("Initial seekd failed, player will go faster instead");
      client->position = 0;
    }
  }

  /* Set base time considering seek position after seek */
  gst_element_set_base_time (client->player,
      client->base_time + client->position);

  /* Before we start playing, ensure we have selected the right audio track */
  set_language (client);

  if (!client->paused)
    gst_element_set_state (client->player, GST_STATE_PLAYING);
}

static void
handle_player_set_media_message (AurClient * client, GstStructure * s)
{
  const gchar *protocol, *path, *language;
  int port;
  gint64 tmp;

  protocol = gst_structure_get_string (s, "resource-protocol");
  path = gst_structure_get_string (s, "resource-path");

  if (protocol == NULL || path == NULL)
    return;                     /* Invalid message */

  if (!aur_json_structure_get_int (s, "resource-port", &port))
    return;

  if (!aur_json_structure_get_int64 (s, "base-time", &tmp))
    return;                     /* Invalid message */
  client->base_time = (GstClockTime) (tmp);

  if (!aur_json_structure_get_int64 (s, "position", &tmp))
    return;                     /* Invalid message */
  client->position = (GstClockTime) (tmp);

  if (!aur_json_structure_get_boolean (s, "paused", &client->paused))
    return;

  g_free (client->language);
  language = gst_structure_get_string (s, "language");
  client->language = g_strdup (language ? language : "en");

  g_free (client->uri);
  client->uri = g_strdup_printf ("%s://%s:%d%s", protocol,
      client->connected_server, port, path);

  if (client->enabled)
    set_media (client);

  g_object_notify (G_OBJECT (client), "language");
  g_object_notify (G_OBJECT (client), "media-uri");
}

static void
handle_player_play_message (AurClient * client, GstStructure * s)
{
  gint64 tmp;

  if (!aur_json_structure_get_int64 (s, "base-time", &tmp))
    return;                     /* Invalid message */

  client->base_time = (GstClockTime) (tmp);
  client->paused = FALSE;

  if (client->enabled && client->player) {
    GST_DEBUG_OBJECT (client,
        "Playing base_time %" GST_TIME_FORMAT " (position %" GST_TIME_FORMAT
        ")", GST_TIME_ARGS (client->base_time),
        GST_TIME_ARGS (client->position));
    gst_element_set_base_time (GST_ELEMENT (client->player),
        client->base_time + client->position);

    gst_element_set_state (GST_ELEMENT (client->player), GST_STATE_PLAYING);
  }

  g_object_notify (G_OBJECT (client), "paused");
  g_object_notify (G_OBJECT (client), "base-time");
}

static void
handle_player_pause_message (AurClient * client, GstStructure * s)
{
  GstClockTime old_position = client->position;
  gint64 tmp;

  if (!aur_json_structure_get_int64 (s, "position", &tmp))
    return;                     /* Invalid message */

  client->position = (GstClockTime) (tmp);
  client->paused = TRUE;

  if (client->enabled && client->player) {
    GST_DEBUG_OBJECT (client, "Pausing at position %" GST_TIME_FORMAT,
        GST_TIME_ARGS (client->position));
    gst_element_set_state (GST_ELEMENT (client->player), GST_STATE_PAUSED);
    if (!gst_element_seek_simple (client->player, GST_FORMAT_TIME,
            GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, client->position)) {
      g_warning ("Pausing seek failed");
      client->position = old_position;
    }
  }

  g_object_notify (G_OBJECT (client), "paused");
  g_object_notify (G_OBJECT (client), "position");
}

static void
handle_player_seek_message (AurClient * client, GstStructure * s)
{
  GstClockTime old_position = client->position;
  gint64 tmp;

  if (!aur_json_structure_get_int64 (s, "base-time", &tmp))
    return;                     /* Invalid message */
  client->base_time = (GstClockTime) tmp;

  if (!aur_json_structure_get_int64 (s, "position", &tmp))
    return;                     /* Invalid message */
  client->position = (GstClockTime) (tmp);

  if (client->enabled && client->player) {
    GST_DEBUG_OBJECT (client,
        "Seeking to position %" GST_TIME_FORMAT " (base_time %" GST_TIME_FORMAT
        ")", GST_TIME_ARGS (client->position),
        GST_TIME_ARGS (client->base_time));

    if (!gst_element_seek_simple (client->player, GST_FORMAT_TIME,
            GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, client->position)) {
      g_warning ("Seeking failed, client may run faster or block.");
      client->position = old_position;
    }

    gst_element_set_base_time (client->player,
        client->base_time + client->position);
  }

  g_object_notify (G_OBJECT (client), "base-time");
  g_object_notify (G_OBJECT (client), "position");
}

static void
handle_player_set_volume_message (AurClient * client, GstStructure * s)
{
  gdouble new_vol;

  if (!aur_json_structure_get_double (s, "level", &new_vol))
    return;

  if (client->player == NULL)
    construct_player (client);

  if (client->player) {
    // g_print ("New volume %g\n", new_vol);
    g_object_set (G_OBJECT (client->player), "volume", new_vol,
        "mute", (gboolean) (new_vol == 0.0), NULL);
  }
}

static void
handle_player_set_client_message (AurClient * client, GstStructure * s)
{
  gboolean enabled;

  if (!aur_json_structure_get_boolean (s, "enabled", &enabled))
    return;

  if (enabled == client->enabled)
    return;
  client->enabled = enabled;

  if (!client->player)
    return;

  if (client->enabled && client->uri)
    set_media (client);
  else
    gst_element_set_state (GST_ELEMENT (client->player), GST_STATE_READY);

  g_object_notify (G_OBJECT (client), "enabled");
}

static void
handle_player_language_message (AurClient * client, GstStructure * s)
{
  const gchar *language;

  language = gst_structure_get_string (s, "language");
  if (!language)
    return;

  g_free (client->language);
  client->language = g_strdup (language);

  if (client->enabled && client->player)
    set_language (client);

  g_object_notify (G_OBJECT (client), "language");
}

static void
setup_record_rtpbin (GstElement * rtspsink G_GNUC_UNUSED,
    GstElement * rtpbin, AurClient * client)
{
  GST_INFO_OBJECT (client, "Configuring new rtpbin %" GST_PTR_FORMAT, rtpbin);

  if (g_object_class_find_property (G_OBJECT_GET_CLASS (rtpbin),
          "rtcp-sync-send-time")) {
    g_object_set (rtpbin, "rtcp-sync-send-time", FALSE,
        "max-rtcp-rtp-time-diff", -1, NULL);
  } else {
    GST_WARNING_OBJECT (client,
        "rtpbin did not have rtcp-sync-send-time property. Outdated GStreamer.");
  }
}

static void
handle_client_record_message (AurClient * client, GstStructure * s)
{
  gboolean enabled;
  const gchar *path;
  int port;
  gchar *dest;

  path = gst_structure_get_string (s, "record-path");

  if (path == NULL)
    return;                     /* Invalid message */

  if (!aur_json_structure_get_int (s, "record-port", &port))
    return;

  if (!aur_json_structure_get_boolean (s, "enabled", &enabled))
    return;

  if (!enabled) {
    if (client->record_pipe) {
      GST_DEBUG_OBJECT (client, "Destroying recorder pipeline");
      gst_element_set_state (client->record_pipe, GST_STATE_NULL);
      gst_object_unref (GST_OBJECT (client->record_pipe));
      client->record_pipe = NULL;
    }
    return;
  }

  dest = g_strdup_printf ("rtsp://%s:%u%s",
      client->connected_server, port, path);

  if (client->record_pipe == NULL ||
      client->record_dest == NULL || !g_str_equal (dest, client->record_dest)) {
    GError *error = NULL;
    GstElement *rtspsink;

    g_free (client->record_dest);
    client->record_dest = g_strdup (dest);

    if (client->record_pipe == NULL) {
#ifndef ANDROID
      const gchar *audiosrc = "autoaudiosrc";
#else
      const gchar *audiosrc = "openslessrc preset=voice-recognition";
#endif
      gchar *pipe_str;
      GstElement *rtspsink;

      pipe_str =
          g_strdup_printf
          ("%s buffer-time=60000 ! audioconvert ! audioresample ! audio/x-raw,format=S16LE,rate=48000 ! opusenc frame-size=10 ! rtspsink ntp-time-source=3 name=rtspsink",
          audiosrc);
      client->record_pipe = gst_parse_launch (pipe_str, &error);
      g_free (pipe_str);

      if (error != NULL) {
        GST_ERROR_OBJECT (client, "Failed to create record pipe. Error: %s",
            error->message);
        g_error_free (error);
        goto fail;
      }
      gst_pipeline_use_clock (GST_PIPELINE (client->record_pipe),
          client->net_clock);

      rtspsink =
          gst_bin_get_by_name (GST_BIN (client->record_pipe), "rtspsink");
      if (rtspsink == NULL) {
        GST_ERROR_OBJECT (client, "Failed to retrieve rtspsink element");
        goto fail;
      }
      g_signal_connect (rtspsink, "new-manager",
          (GCallback) setup_record_rtpbin, client);
      gst_object_unref (rtspsink);
    } else {
      gst_element_set_state (client->record_pipe, GST_STATE_NULL);
    }
    g_return_if_fail (client->record_pipe != NULL);

    rtspsink = gst_bin_get_by_name (GST_BIN (client->record_pipe), "rtspsink");
    g_return_if_fail (rtspsink != NULL);

    g_object_set (rtspsink, "location", dest, NULL);
    GST_INFO_OBJECT (client, "Setting Record pipe destination to %s", dest);
  }

  gst_element_set_state (GST_ELEMENT (client->record_pipe), GST_STATE_PLAYING);
  return;

fail:
  if (client->record_pipe) {
    gst_element_set_state (GST_ELEMENT (client->record_pipe), GST_STATE_NULL);
    gst_object_unref (client->record_pipe);
    client->record_pipe = NULL;
  }
}

static void
handle_player_message (AurClient * client, GstStructure * s)
{
  const gchar *msg_type;

  msg_type = gst_structure_get_string (s, "msg-type");
  if (msg_type == NULL || g_str_equal (msg_type, "ping"))
    return;

  if (g_str_equal (msg_type, "enrol"))
    handle_player_enrol_message (client, s);
  else if (g_str_equal (msg_type, "set-media"))
    handle_player_set_media_message (client, s);
  else if (g_str_equal (msg_type, "play"))
    handle_player_play_message (client, s);
  else if (g_str_equal (msg_type, "pause"))
    handle_player_pause_message (client, s);
  else if (g_str_equal (msg_type, "volume"))
    handle_player_set_volume_message (client, s);
  else if (g_str_equal (msg_type, "client-setting"))
    handle_player_set_client_message (client, s);
  else if (g_str_equal (msg_type, "seek"))
    handle_player_seek_message (client, s);
  else if (g_str_equal (msg_type, "language"))
    handle_player_language_message (client, s);
  else if (g_str_equal (msg_type, "record"))
    handle_client_record_message (client, s);
  else
    g_print ("Unhandled player event of type %s\n", msg_type);
}

static void
handle_controller_enrol_message (AurClient * client, GstStructure * s)
{
  if (aur_json_structure_get_double (s, "volume-level", &client->volume))
    g_object_notify (G_OBJECT (client), "volume");

  if (!(client->flags & AUR_CLIENT_PLAYER)) {
    if (aur_json_structure_get_boolean (s, "paused", &client->paused))
      g_object_notify (G_OBJECT (client), "paused");
  }
}

static void
handle_player_info (G_GNUC_UNUSED SoupSession * session, SoupMessage * msg,
    AurClient * client)
{
  SoupBuffer *buffer;

  if (msg->status_code < 200 || msg->status_code >= 300)
    return;

  buffer = soup_message_body_flatten (msg->response_body);
  if (json_parser_load_from_data (client->json, buffer->data, buffer->length,
          NULL)) {
    const GValue *v1;
    GArray *player_info = NULL;
    gsize i;
    JsonNode *root = json_parser_get_root (client->json);
    GstStructure *s1 = aur_json_to_gst_structure (root);

    if (s1 == NULL)
      return;                   /* Invalid chunk */

    v1 = gst_structure_get_value (s1, "player-clients");
    if (!GST_VALUE_HOLDS_ARRAY (v1))
      goto failed;

    player_info = g_array_sized_new (TRUE, TRUE,
        sizeof (AurPlayerInfo), gst_value_array_get_size (v1));

    for (i = 0; i < gst_value_array_get_size (v1); i++) {
      AurPlayerInfo info;
      const GValue *v2 = gst_value_array_get_value (v1, i);
      const GstStructure *s2;
      gint64 client_id;

      if (!GST_VALUE_HOLDS_STRUCTURE (v2))
        goto failed;

      s2 = gst_value_get_structure (v2);
      if (!aur_json_structure_get_int64 (s2, "client-id", &client_id))
        goto failed;
      info.id = client_id;

      if (!aur_json_structure_get_boolean (s2, "enabled", &info.enabled))
        goto failed;

      if (!aur_json_structure_get_double (s2, "volume", &info.volume))
        goto failed;

      if (!(info.host = g_strdup (gst_structure_get_string (s2, "host"))))
        goto failed;

      g_array_append_val (player_info, info);
    }

    free_player_info (client->player_info);
    client->player_info = player_info;
    player_info = NULL;

    g_signal_emit (client, signals[SIGNAL_PLAYER_INFO_CHANGED], 0);

  failed:
    if (player_info)
      free_player_info (player_info);
    gst_structure_free (s1);
  }
}

static void
refresh_clients_array (AurClient * client)
{
  SoupMessage *soup_msg;
  gchar *uri;

  if (client->shutting_down)
    return;

  uri = g_strdup_printf ("http://%s:%u/client/player_info",
      client->connected_server, client->connected_port);
  soup_msg = soup_message_new ("GET", uri);
  soup_session_queue_message (client->soup, soup_msg,
      (SoupSessionCallback) handle_player_info, client);

  g_free (uri);
}

static void
handle_controller_set_media_message (AurClient * client, GstStructure * s)
{
  if (!(client->flags & AUR_CLIENT_PLAYER))
    handle_player_set_media_message (client, s);

}

static void
handle_controller_play_message (AurClient * client, GstStructure * s)
{
  if (!(client->flags & AUR_CLIENT_PLAYER))
    handle_player_play_message (client, s);
}

static void
handle_controller_pause_message (AurClient * client, GstStructure * s)
{
  if (!(client->flags & AUR_CLIENT_PLAYER))
    handle_player_pause_message (client, s);
}

static void
handle_controller_seek_message (AurClient * client, GstStructure * s)
{
  if (!(client->flags & AUR_CLIENT_PLAYER))
    handle_player_seek_message (client, s);
}

static void
handle_controller_client_volume_message (AurClient * client, GstStructure * s)
{
  gint64 client_id;
  gdouble new_vol;
  gsize i;

  if (!client->player_info)
    return;

  if (!aur_json_structure_get_int64 (s, "client-id", &client_id))
    return;

  if (!aur_json_structure_get_double (s, "level", &new_vol))
    return;

  for (i = 0; i < client->player_info->len; i++) {
    AurPlayerInfo *info;
    info = &g_array_index (client->player_info, AurPlayerInfo, i);
    if (info->id == (guint) client_id) {
      info->volume = new_vol;
      g_signal_emit (client, signals[SIGNAL_CLIENT_VOLUME_CHANGED], 0,
          info->id, info->volume);
      return;
    }
  }
}

static void
handle_controller_volume_message (AurClient * client, GstStructure * s)
{
  if (!aur_json_structure_get_double (s, "level", &client->volume))
    return;

  g_object_notify (G_OBJECT (client), "volume");
}

static void
handle_controller_client_setting_message (AurClient * client, GstStructure * s)
{
  gint64 client_id;
  gboolean enabled;
  gsize i;

  if (!client->player_info)
    return;

  if (!aur_json_structure_get_int64 (s, "client-id", &client_id))
    return;

  if (!aur_json_structure_get_boolean (s, "enabled", &enabled))
    return;

  for (i = 0; i < client->player_info->len; i++) {
    AurPlayerInfo *info;
    info = &g_array_index (client->player_info, AurPlayerInfo, i);
    if (info->id == (guint) client_id) {
      info->enabled = enabled;
      g_signal_emit (client, signals[SIGNAL_CLIENT_SETTING_CHANGED], 0,
          info->id, info->enabled);
      return;
    }
  }
}

static void
handle_controller_language_message (AurClient * client, GstStructure * s)
{
  if (!(client->flags & AUR_CLIENT_PLAYER))
    handle_player_language_message (client, s);
}

static void
handle_controller_message (AurClient * client, GstStructure * s)
{
  const gchar *msg_type;

  msg_type = gst_structure_get_string (s, "msg-type");
  if (msg_type == NULL || g_str_equal (msg_type, "ping"))
    return;

  if (g_str_equal (msg_type, "enrol"))
    handle_controller_enrol_message (client, s);
  else if (g_str_equal (msg_type, "player-clients-changed"))
    refresh_clients_array (client);
  else if (g_str_equal (msg_type, "client-setting"))
    handle_controller_client_setting_message (client, s);
  else if (g_str_equal (msg_type, "client-volume"))
    handle_controller_client_volume_message (client, s);
  else if (g_str_equal (msg_type, "volume"))
    handle_controller_volume_message (client, s);
  else if (g_str_equal (msg_type, "play"))
    handle_controller_play_message (client, s);
  else if (g_str_equal (msg_type, "pause"))
    handle_controller_pause_message (client, s);
  else if (g_str_equal (msg_type, "seek"))
    handle_controller_seek_message (client, s);
  else if (g_str_equal (msg_type, "set-media"))
    handle_controller_set_media_message (client, s);
  else if (g_str_equal (msg_type, "language"))
    handle_controller_language_message (client, s);
  else {
    GST_WARNING_OBJECT (client, "Unhandled contorller event of type %s",
        msg_type);
  }
}

static void
handle_received_chunk (SoupMessage * msg, SoupBuffer * chunk,
    AurClient * client)
{
  const gchar *ptr;
  gsize length;
  AurClientFlags flag = get_flag_from_msg (msg);
  JsonNode *root;
  GstStructure *s;
  GError *err = NULL;
  gchar *json_str = NULL;

  if (client->was_connected & flag) {
    g_print ("Successfully connected %s to server %s:%d\n",
        flag == AUR_CLIENT_PLAYER ? "player" : "controller",
        client->connected_server, client->connected_port);
    client->was_connected |= flag;
  }

  /* Set up or re-trigger 20 second idle timeout for ping messages */
  if (client->idle_timeout)
    g_source_destroy (client->idle_timeout);
  client->idle_timeout =
      aur_client_new_timeout (client, 20, (GSourceFunc) conn_idle_timeout);

#if HAVE_AVAHI
  /* Successful server connection, stop avahi discovery */
  if (client->avahi_client) {
    avahi_client_free (client->avahi_client);
    client->avahi_sb = NULL;
    client->avahi_client = NULL;
  }
#endif

  if (client->json == NULL)
    client->json = json_parser_new ();

  ptr = memchr (chunk->data, '\0', chunk->length);
  if (!ptr)
    return;

  /* Save remaining portion */
  ptr += 1;
  length = (chunk->length - (ptr - chunk->data));

  chunk = soup_message_body_flatten (msg->response_body);

  // Ignore null string chunks
  if (chunk->length < 2)
    goto end;

  /* Workaround: Copy to a string to avoid stupid
   * UTF-8 validation bug in json-glib 1.0.2 */
  json_str = g_strndup (chunk->data, chunk->length);

#if 0
  g_print ("%s\n", json_str);
#endif

  if (!json_parser_load_from_data (client->json, json_str, -1,
          &err) || err != NULL)
    goto fail;

  root = json_parser_get_root (client->json);
  s = aur_json_to_gst_structure (root);

  if (s == NULL)
    goto fail;                  /* Invalid chunk */

  if (flag == AUR_CLIENT_PLAYER)
    handle_player_message (client, s);
  else
    handle_controller_message (client, s);

  gst_structure_free (s);

end:
  g_free (json_str);

  soup_message_body_truncate (msg->response_body);
  /* Put back remaining part */
  if (length)
    soup_message_body_append (msg->response_body, SOUP_MEMORY_COPY, ptr,
        length);
  return;

fail:{
    g_print ("Failed to parse message '%s'\n", json_str);
    if (err) {
      g_print ("Error: %s\n", err->message);
      g_error_free (err);
    }
    goto end;
  }
}

static void
connect_to_server (AurClient * client, const gchar * server, int port)
{
  SoupMessage *msg;
  char *uri;

  g_free (client->connected_server);
  client->connected_server = g_strdup (server);
  client->connected_port = port;

  if (client->shutting_down)
    return;

  g_print ("In connect_to_server(%s,%d), client->flags %u, connecting %u\n",
      server, port, client->flags, client->connecting);

  if (client->flags & AUR_CLIENT_PLAYER
      && !(client->connecting & AUR_CLIENT_PLAYER)) {
    client->connecting |= AUR_CLIENT_PLAYER;

    uri = g_strdup_printf ("http://%s:%u/client/player_events", server, port);
    GST_DEBUG_OBJECT (client, "Attempting to connect player to server %s:%d",
        server, port);
    msg = soup_message_new ("GET", uri);
    g_signal_connect (msg, "got-chunk", (GCallback) handle_received_chunk,
        client);
    soup_session_queue_message (client->soup, msg,
        (SoupSessionCallback) handle_connection_closed_cb, client);
    g_free (uri);
  }

  if (client->flags & AUR_CLIENT_CONTROLLER
      && !(client->connecting & AUR_CLIENT_CONTROLLER)) {
    GST_DEBUG_OBJECT (client,
        "Attempting to connect controller to server %s:%d", server, port);
    client->connecting |= AUR_CLIENT_CONTROLLER;

    uri = g_strdup_printf ("http://%s:%u/client/control_events", server, port);
    msg = soup_message_new ("GET", uri);
    g_signal_connect (msg, "got-chunk", (GCallback) handle_received_chunk,
        client);
    soup_session_queue_message (client->soup, msg,
        (SoupSessionCallback) handle_connection_closed_cb, client);
    g_free (uri);
  }

  g_object_notify (G_OBJECT (client), "connected-server");
}

static void
aur_client_init (AurClient * client)
{
  client->server_port = 5457;
  client->paused = TRUE;
}

static void
aur_client_constructed (GObject * object)
{
  AurClient *client = (AurClient *) (object);
  gint max_con = 1;

  if (G_OBJECT_CLASS (aur_client_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (aur_client_parent_class)->constructed (object);

  client->soup =
      soup_session_async_new_with_options (SOUP_SESSION_ASYNC_CONTEXT,
      client->context, NULL);
  g_assert (client->soup);
  if (!g_strcmp0 ("1", g_getenv ("AURENA_DEBUG")))
    soup_session_add_feature (client->soup,
        SOUP_SESSION_FEATURE (soup_logger_new (SOUP_LOGGER_LOG_BODY, -1)));

  /* Set a 20 second timeout on pings from the server */
  g_object_set (G_OBJECT (client->soup), "idle-timeout", 20, NULL);
  /* 5 second timeout before retrying with new connections */
  g_object_set (G_OBJECT (client->soup), "timeout", 5, NULL);

  if (client->flags & AUR_CLIENT_PLAYER)
    max_con++;

  if (client->flags & AUR_CLIENT_CONTROLLER)
    max_con++;

  g_object_set (client->soup, "max-conns-per-host", max_con, NULL);

  try_reconnect (client);
}

static void
aur_client_class_init (AurClientClass * client_class)
{
  GObjectClass *gobject_class = (GObjectClass *) (client_class);

  gobject_class->constructed = aur_client_constructed;
  gobject_class->dispose = aur_client_dispose;
  gobject_class->finalize = aur_client_finalize;

  gobject_class->set_property = aur_client_set_property;
  gobject_class->get_property = aur_client_get_property;

  g_object_class_install_property (gobject_class, PROP_SERVER_HOST,
      g_param_spec_string ("server-host", "Aurena Server",
          "Aurena Server hostname or IP", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FLAGS,
      g_param_spec_uint ("flags", "Client Flags",
          "Aurena Client flags to enable player and controller mode", 0,
          G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ASYNC_MAIN_CONTEXT,
      g_param_spec_boxed ("main-context", "Async Main Context",
          "GLib Main Context to use for HTTP connections",
          G_TYPE_MAIN_CONTEXT, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (gobject_class, PROP_PAUSED,
      g_param_spec_boolean ("paused", "paused",
          "True if Aurena is paused, playing otherwise", TRUE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BASE_TIME,
      g_param_spec_uint64 ("base-time", "Server Base Time",
          "The server time of when the playback has started",
          0, G_MAXUINT64, GST_CLOCK_TIME_NONE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_POSITION,
      g_param_spec_uint64 ("position", "Position",
          "Playback position when paused", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MEDIA_URI,
      g_param_spec_string ("media-uri", "Media URI",
          "URI of the currently playing media", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VOLUME,
      g_param_spec_double ("volume", "volume",
          "Main volume", 0.0, 10.0, 0.0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CONNECTED_SERVER,
      g_param_spec_string ("connected-server", "Aurena Connected Server",
          "Aurena Connected Server hostname or IP", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENABLED,
      g_param_spec_boolean ("enabled", "enabled",
          "True if Aurena is enabled", FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LANGUAGE,
      g_param_spec_string ("language", "Audio Language",
          "Audio language to choose", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  signals[SIGNAL_PLAYER_CREATED] = g_signal_new ("player-created",
      G_TYPE_FROM_CLASS (client_class), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      NULL, G_TYPE_NONE, 1, GST_TYPE_ELEMENT);

  signals[SIGNAL_CLIENT_VOLUME_CHANGED] =
      g_signal_new ("client-volume-changed", G_TYPE_FROM_CLASS (client_class),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_UINT,
      G_TYPE_DOUBLE);

  signals[SIGNAL_CLIENT_SETTING_CHANGED] =
      g_signal_new ("client-setting-changed",
      G_TYPE_FROM_CLASS (client_class), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      NULL, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_BOOLEAN);

  signals[SIGNAL_PLAYER_INFO_CHANGED] = g_signal_new ("player-info-changed",
      G_TYPE_FROM_CLASS (client_class), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      NULL, G_TYPE_NONE, 0);
}

static void
aur_client_finalize (GObject * object)
{
  AurClient *client = (AurClient *) (object);

#if HAVE_AVAHI
  if (client->avahi_sb)
    avahi_service_browser_free (client->avahi_sb);
  if (client->avahi_client)
    avahi_client_free (client->avahi_client);
  if (client->glib_poll)
    avahi_glib_poll_free (client->glib_poll);
#endif

  if (client->net_clock)
    gst_object_unref (client->net_clock);
  if (client->soup)
    g_object_unref (client->soup);
  if (client->json)
    g_object_unref (client->json);
  if (client->player) {
    gst_object_unref (client->player);
  }
  if (client->context)
    g_main_context_unref (client->context);

  g_free (client->server_host);
  g_free (client->connected_server);
  g_free (client->uri);
  g_free (client->language);
  g_free (client->record_dest);
  free_player_info (client->player_info);

  G_OBJECT_CLASS (aur_client_parent_class)->finalize (object);
}

static void
aur_client_dispose (GObject * object)
{
  AurClient *client = (AurClient *) (object);

  client->shutting_down = TRUE;

  if (client->soup)
    soup_session_abort (client->soup);
  if (client->player)
    gst_element_set_state (client->player, GST_STATE_NULL);

  if (client->record_pipe) {
    gst_element_set_state (client->record_pipe, GST_STATE_NULL);
    gst_object_unref (GST_OBJECT (client->record_pipe));
    client->record_pipe = NULL;
  }

  G_OBJECT_CLASS (aur_client_parent_class)->dispose (object);
}

static void
split_server_host (AurClient * client)
{
  /* See if the client->server_host string has a : and split into
   * server:port if so */
  gchar *sep = g_strrstr (client->server_host, ":");

  if (sep) {
    gchar *server = g_strndup (client->server_host, sep - client->server_host);

    client->server_port = atoi (sep + 1);
    g_free (client->server_host);
    client->server_host = server;
  }
}

static void
aur_client_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  AurClient *client = (AurClient *) (object);

  switch (prop_id) {
    case PROP_SERVER_HOST:{
      if (client->server_host)
        g_free (client->server_host);
      client->server_host = g_value_dup_string (value);
      if (client->server_host)
        split_server_host (client);
      break;
    }
    case PROP_FLAGS:{
      client->flags = g_value_get_uint (value);
      break;
    }
    case PROP_ASYNC_MAIN_CONTEXT:{
      if (client->context)
        g_main_context_unref (client->context);
      client->context = (GMainContext *) (g_value_dup_boxed (value));
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
aur_client_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  AurClient *client = (AurClient *) (object);

  switch (prop_id) {
    case PROP_SERVER_HOST:{
      char *tmp = NULL;
      if (client->server_host)
        tmp =
            g_strdup_printf ("%s:%u", client->server_host, client->server_port);
      g_value_take_string (value, tmp);
      break;
    }
    case PROP_FLAGS:{
      g_value_set_uint (value, client->flags);
      break;
    }
    case PROP_PAUSED:{
      g_value_set_boolean (value, client->paused);
      break;
    }
    case PROP_BASE_TIME:{
      g_value_set_uint64 (value, client->base_time);
      break;
    }
    case PROP_POSITION:{
      g_value_set_uint64 (value, client->position);
      break;
    }
    case PROP_MEDIA_URI:{
      g_value_set_string (value, client->uri);
      break;
    }
    case PROP_VOLUME:{
      g_value_set_double (value, client->volume);
      break;
    }
    case PROP_CONNECTED_SERVER:{
      char *tmp = NULL;
      if (client->connected_server)
        tmp = g_strdup_printf ("%s:%u", client->connected_server,
            client->connected_port);
      g_value_take_string (value, tmp);
      break;
    }
    case PROP_LANGUAGE:{
      g_value_set_string (value, client->language);
      break;
    }
    case PROP_ASYNC_MAIN_CONTEXT:{
      g_value_set_boxed (value, client->context);
      break;
    }

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#if HAVE_AVAHI
static void
avahi_resolve_callback (AvahiServiceResolver * r,
    AVAHI_GCC_UNUSED AvahiIfIndex interface,
    AVAHI_GCC_UNUSED AvahiProtocol protocol, AvahiResolverEvent event,
    AVAHI_GCC_UNUSED const char *name, AVAHI_GCC_UNUSED const char *type,
    AVAHI_GCC_UNUSED const char *domain, const char *host_name,
    AVAHI_GCC_UNUSED const AvahiAddress * address, uint16_t port,
    AVAHI_GCC_UNUSED AvahiStringList * txt,
    AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
    AVAHI_GCC_UNUSED void *userdata)
{
  AurClient *client = userdata;

  switch (event) {
    case AVAHI_RESOLVER_FAILURE:
      break;

    case AVAHI_RESOLVER_FOUND:{
      /* FIXME: Build a list of servers and try each one in turn? */
      if (client->server_host == NULL) {
        client->server_host = g_strdup (host_name);
        client->server_port = port;
        connect_to_server (client, host_name, port);
      }
    }
  }

  avahi_service_resolver_free (r);
}

static void
browse_callback (AVAHI_GCC_UNUSED AvahiServiceBrowser * b,
    AvahiIfIndex interface, AvahiProtocol protocol,
    AvahiBrowserEvent event,
    const char *name, const char *type, const char *domain,
    AVAHI_GCC_UNUSED AvahiLookupResultFlags flags, void *userdata)
{
  AurClient *client = userdata;

  switch (event) {
    case AVAHI_BROWSER_FAILURE:
      /* Respawn browser on a timer? */
      avahi_service_browser_free (client->avahi_sb);
      client->recon_timeout =
          aur_client_new_timeout (client, 1, (GSourceFunc) try_reconnect);
      return;

    case AVAHI_BROWSER_NEW:{
      avahi_service_resolver_new (client->avahi_client, interface,
          protocol, name, type, domain, AVAHI_PROTO_UNSPEC, 0,
          avahi_resolve_callback, client);
      break;
    }
    case AVAHI_BROWSER_REMOVE:
    case AVAHI_BROWSER_ALL_FOR_NOW:
    case AVAHI_BROWSER_CACHE_EXHAUSTED:
      break;
  }
}

static void
aur_avahi_client_callback (AvahiClient * s, AvahiClientState state,
    AurClient * client)
{
  switch (state) {
    case AVAHI_CLIENT_S_RUNNING:{
      if (client->avahi_sb == NULL) {
        GST_INFO ("Looking for new broadcast servers");
        client->avahi_sb = avahi_service_browser_new (s, AVAHI_IF_UNSPEC,
            AVAHI_PROTO_UNSPEC, "_aurena._tcp", NULL, 0, browse_callback,
            client);
        if (client->avahi_sb == NULL) {
          fprintf (stderr, "Failed to create service browser: %s\n",
              avahi_strerror (avahi_client_errno (client->avahi_client)));
        }
      }
      break;
    }
    default:
      break;
  }
}

static void
search_for_server (AurClient * client)
{
  const AvahiPoll *poll_api;
  int error;

  if (client->glib_poll == NULL) {
    client->glib_poll = avahi_glib_poll_new (NULL, G_PRIORITY_DEFAULT);
    if (client->glib_poll == NULL)
      return;
  }

  poll_api = avahi_glib_poll_get (client->glib_poll);

  if (client->avahi_client == NULL) {
    client->avahi_client =
        avahi_client_new (poll_api, AVAHI_CLIENT_NO_FAIL,
        (AvahiClientCallback) aur_avahi_client_callback, client, &error);
    if (client->avahi_client == NULL) {
      fprintf (stderr, "Failed to connect to Avahi: %s",
          avahi_strerror (error));
      return;
    }
  }

}
#else
static void
search_for_server (AurClient * client)
{
}
#endif

AurClient *
aur_client_new (GMainContext * context, const char *server_host,
    AurClientFlags flags)
{
  AurClient *client = g_object_new (AUR_TYPE_CLIENT,
      "main-context", context,
      "server-host", server_host,
      "flags", flags,
      NULL);

  return client;
}

gboolean
aur_client_is_connected (AurClient * client)
{
  return (client->connected_server != NULL);
}

gboolean
aur_client_is_enabled (AurClient * client)
{
  return client->enabled;
}

gboolean
aur_client_is_playing (AurClient * client)
{
  return !client->paused;
}

static void
aur_client_submit_msg (AurClient * client, SoupMessage * msg)
{
  if (client->shutting_down)
    g_object_unref (msg);
  else
    soup_session_queue_message (client->soup, msg, NULL, NULL);
}

void
aur_client_set_media (AurClient * client, const gchar * id)
{
  SoupMessage *soup_msg;
  gchar *uri = g_strdup_printf ("http://%s:%u/control/next",
      client->connected_server, client->connected_port);

  if (id)
    soup_msg = soup_form_request_new ("POST", uri, "id", id, NULL);
  else
    soup_msg = soup_message_new ("GET", uri);

  aur_client_submit_msg (client, soup_msg);

  g_free (uri);
}

void
aur_client_next (AurClient * client, guint id)
{
  gchar *id_str = NULL;
  if (id)
    id_str = g_strdup_printf ("%u", id);
  aur_client_set_media (client, id_str);
  g_free (id_str);
}

void
aur_client_play (AurClient * client)
{
  SoupMessage *soup_msg;
  gchar *uri;

  uri = g_strdup_printf ("http://%s:%u/control/play",
      client->connected_server, client->connected_port);
  soup_msg = soup_message_new ("GET", uri);
  aur_client_submit_msg (client, soup_msg);

  g_free (uri);
}

void
aur_client_pause (AurClient * client)
{
  SoupMessage *soup_msg;
  gchar *uri;

  uri = g_strdup_printf ("http://%s:%u/control/pause",
      client->connected_server, client->connected_port);
  soup_msg = soup_message_new ("GET", uri);
  aur_client_submit_msg (client, soup_msg);

  g_free (uri);
}

void
aur_client_seek (AurClient * client, GstClockTime position)
{
  SoupMessage *soup_msg;
  gchar *position_str;
  gchar *uri;

  uri = g_strdup_printf ("http://%s:%u/control/seek",
      client->connected_server, client->connected_port);
  position_str = g_strdup_printf ("%" G_GUINT64_FORMAT, position);
  soup_msg = soup_form_request_new ("POST", uri, "position", position_str,
      NULL);
  aur_client_submit_msg (client, soup_msg);

  g_free (position_str);
  g_free (uri);
}

void
aur_client_set_volume (AurClient * client, gdouble volume)
{
  SoupMessage *soup_msg;
  gchar volume_str[G_ASCII_DTOSTR_BUF_SIZE];
  gchar *uri;

  uri = g_strdup_printf ("http://%s:%u/control/volume",
      client->connected_server, client->connected_port);
  g_ascii_dtostr (volume_str, sizeof (volume_str), volume);
  soup_msg = soup_form_request_new ("POST", uri, "level", volume_str, NULL);
  aur_client_submit_msg (client, soup_msg);

  g_free (uri);
}

const GArray *
aur_client_get_player_info (AurClient * client)
{
  return client->player_info;
}

gboolean
aur_client_get_player_enabled (AurClient * client, guint id)
{
  gsize i;

  for (i = 0; i < client->player_info->len; i++) {
    AurPlayerInfo *info;
    info = &g_array_index (client->player_info, AurPlayerInfo, i);
    if (info->id == id)
      return info->enabled;
  }

  g_warn_if_reached ();
  return FALSE;
}

void
aur_client_set_player_enabled (AurClient * client, guint id, gboolean enabled)
{
  gchar *uri;
  gchar *id_str;
  SoupMessage *soup_msg;

  uri = g_strdup_printf ("http://%s:%u/control/setclient",
      client->connected_server, client->connected_port);
  id_str = g_strdup_printf ("%u", id);
  soup_msg =
      soup_form_request_new ("POST", uri, "client_id", id_str, "enable",
      enabled ? "1" : "0", NULL);
  aur_client_submit_msg (client, soup_msg);

  g_free (id_str);
  g_free (uri);
}

void
aur_client_set_player_volume (AurClient * client, guint id, gdouble volume)
{
  gchar *uri;
  gchar volume_str[G_ASCII_DTOSTR_BUF_SIZE];
  gchar *id_str;
  SoupMessage *soup_msg;

  uri = g_strdup_printf ("http://%s:%u/control/volume",
      client->connected_server, client->connected_port);
  id_str = g_strdup_printf ("%u", id);
  g_ascii_dtostr (volume_str, sizeof (volume_str), volume);
  soup_msg = soup_form_request_new ("POST", uri, "client_id", id_str, "level",
      volume_str, NULL);
  aur_client_submit_msg (client, soup_msg);

  g_free (id_str);
  g_free (uri);
}

void
aur_client_set_language (AurClient * client, const gchar * language_code)
{
  gchar *uri;
  SoupMessage *soup_msg;
  uri = g_strdup_printf ("http://%s:%u/control/language",
      client->connected_server, client->connected_port);
  soup_msg = soup_form_request_new ("POST", uri, "language", language_code,
      NULL);
  aur_client_submit_msg (client, soup_msg);
  g_free (uri);
}
