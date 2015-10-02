/* GStreamer
 * Copyright (C) 2015 Jan Schmidt <jan@centricular.com>
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
 * Aurena Receiver manages incoming audio feeds
 * mixes them and outputs the 8-channel stream for ManyEars
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/app/gstappsink.h>

#include "aur-receiver-ingest.h"

GST_DEBUG_CATEGORY_STATIC (ingest_debug);
#define GST_CAT_DEFAULT ingest_debug

static GstRTSPMedia *construct_media (GstRTSPMediaFactory * factory,
    const GstRTSPUrl * url);
static GstElement *create_element (GstRTSPMediaFactory * mf,
    const GstRTSPUrl * url);

static void
_do_init ()
{
  GST_DEBUG_CATEGORY_INIT (ingest_debug, "aurena/ingest", 0,
      "Aurena Audio Ingest debug");
}

G_DEFINE_TYPE_WITH_CODE (AurReceiverIngestFactory, aur_receiver_ingest_factory,
    GST_TYPE_RTSP_MEDIA_FACTORY, _do_init ());

static gboolean custom_setup_rtpbin (GstRTSPMedia * media, GstElement * rtpbin);
static void aur_receiver_ingest_factory_dispose (GObject * object);

static void
aur_receiver_ingest_factory_class_init (AurReceiverIngestFactoryClass *
    factory_klass)
{
  GObjectClass *object_class = (GObjectClass *) (factory_klass);
  GstRTSPMediaFactoryClass *mf_klass =
      (GstRTSPMediaFactoryClass *) (factory_klass);

  object_class->dispose = aur_receiver_ingest_factory_dispose;
  mf_klass->construct = construct_media;
}

static void
aur_receiver_ingest_factory_init (AurReceiverIngestFactory * factory)
{
  GstRTSPMediaFactory *mf = (GstRTSPMediaFactory *) (factory);
  gst_rtsp_media_factory_set_transport_mode (mf,
      GST_RTSP_TRANSPORT_MODE_RECORD);
  gst_rtsp_media_factory_set_latency (mf, 40);
}

static void
aur_receiver_ingest_factory_dispose (GObject * object)
{
  AurReceiverIngestFactory *factory = AUR_RECEIVER_INGEST_FACTORY (object);

  if (factory->processor) {
    g_object_unref (factory->processor);
    factory->processor = NULL;
  }
}

static void
link_to_output (AurReceiverIngestMedia * media, gint i, GstPad * pad)
{
  gchar *name;
  GstElement *appsink;
  GstPad *sinkpad;

  name = g_strdup_printf ("aursink%d", i);

  appsink = gst_bin_get_by_name (media->bin, name);
  g_free (name);

  g_assert (appsink != NULL);
  sinkpad = gst_element_get_static_pad (appsink, "sink");
  g_assert (sinkpad != NULL);
  g_assert (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (pad, sinkpad)));

  GST_DEBUG_OBJECT (media, "Linked srcpad %" GST_PTR_FORMAT " to appsink", pad);

  gst_object_unref (sinkpad);
}

static void
raw_pad_added_cb (GstElement * deinterleave G_GNUC_UNUSED,
    GstPad * pad, AurReceiverIngestMedia * media)
{
  gint pad_id;

  GST_DEBUG_OBJECT (media, "Got deinterleave pad %" GST_PTR_FORMAT, pad);
  g_assert (sscanf (GST_PAD_NAME (pad), "src_%u", &pad_id) == 1);
  link_to_output (media, pad_id, pad);
}

typedef struct _AurAppSinkClosure
{
  AurReceiverIngestMedia *media;
  gint chanid;
} AurAppSinkClosure;

static void
handle_stream_eos (GstAppSink * appsink, gpointer user_data)
{
  AurAppSinkClosure *info = (AurAppSinkClosure *) (user_data);
  GST_LOG_OBJECT (info->media, "EOS on client %d channel %d", info->media->id,
      info->chanid);
}

static GstFlowReturn
handle_new_sample (GstAppSink * appsink, gpointer user_data)
{
  AurAppSinkClosure *info = (AurAppSinkClosure *) (user_data);
  GST_LOG_OBJECT (info->media, "New sample on client %d channel %d",
      info->media->id, info->chanid);

  return GST_FLOW_OK;
}

static void
release_closure (AurAppSinkClosure * info)
{
  gst_object_unref (info->media);
  g_free (info);
}

static GstAppSinkCallbacks sink_cb = {
  handle_stream_eos,
  NULL,                         /* not interested in preroll samples */
  handle_new_sample,
  {0,}
};

static void
add_output_chain (AurReceiverIngestMedia * media, gint i)
{
  AurAppSinkClosure *info;
  gchar *name;
  GstElement *appsink;
  name = g_strdup_printf ("aursink%d", i);

  appsink = gst_element_factory_make ("appsink", name);
  g_assert (appsink != NULL);

  g_object_set (GST_OBJECT (appsink),
      "async", FALSE, "sync", FALSE, "emit-signals", FALSE, NULL);

  info = g_new0 (AurAppSinkClosure, 1);
  info->media = gst_object_ref (media);
  info->chanid = i;

  gst_app_sink_set_callbacks (GST_APP_SINK_CAST (appsink),
      &sink_cb, info, (GDestroyNotify) release_closure);

  gst_bin_add (media->bin, appsink);
  gst_element_set_state (appsink, GST_STATE_PLAYING);

  GST_DEBUG_OBJECT (media, "Inserting appsink %s", name);
  g_free (name);
}

static void
dec_pad_added_cb (GstElement * db G_GNUC_UNUSED, GstPad * pad,
    AurReceiverIngestMedia * media)
{
  GstCaps *caps;
  GstStructure *s;
  gint channels, i;

  if ((caps = gst_pad_get_current_caps (pad)) == NULL)
    if ((caps = gst_pad_query_caps (pad, NULL)) == NULL)
      goto no_caps;

  GST_DEBUG_OBJECT (media, "Pad %" GST_PTR_FORMAT " with caps %" GST_PTR_FORMAT,
      pad, caps);

  if (!gst_caps_is_fixed (caps)) {
    caps = gst_caps_make_writable (caps);
    caps = gst_caps_fixate (caps);
  }
  s = gst_caps_get_structure (caps, 0);

  if (!gst_structure_has_name (s, "audio/x-raw"))
    goto wrong_caps;
  if (!gst_structure_get_int (s, "channels", &channels))
    goto wrong_caps;
  gst_caps_unref (caps);

  GST_LOG_OBJECT (media, "Have %d channels", channels);
  for (i = 0; i < channels; i++)
    add_output_chain (media, i);

  if (channels > 1) {
    GstElement *deinterleave;
    GstPad *sinkpad;
    GST_DEBUG_OBJECT (media, "Multiple channels. Configuring deinterleave");

    deinterleave = gst_element_factory_make ("deinterleave", NULL);
    if (deinterleave == NULL)
      goto no_deinterleave;
    g_signal_connect (G_OBJECT (deinterleave), "pad-added",
        (GCallback) raw_pad_added_cb, media);
    gst_bin_add (GST_BIN (media->bin), deinterleave);
    gst_element_sync_state_with_parent (deinterleave);
    sinkpad = gst_element_get_static_pad (deinterleave, "sink");
    g_assert (sinkpad != NULL);
    g_assert (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (pad, sinkpad)));
    gst_object_unref (sinkpad);
  } else {
    link_to_output (media, 0, pad);
  }
  return;

  /* ERRORS */
no_deinterleave:
  {
    GST_WARNING_OBJECT (media,
        "Could not create deinterleave element to handle this stream %"
        GST_PTR_FORMAT, pad);
    return;
  }
wrong_caps:
  {
    GST_WARNING_OBJECT (media, "Non-audio caps %" GST_PTR_FORMAT
        "from pad %" GST_PTR_FORMAT, caps, pad);
    gst_caps_unref (caps);
    return;
  }
no_caps:
  {
    GST_WARNING_OBJECT (media, "Could not get caps from pad %" GST_PTR_FORMAT,
        pad);
    return;
  }
}

static GstRTSPMedia *
construct_media (GstRTSPMediaFactory * mf, const GstRTSPUrl * url)
{
  AurReceiverIngestFactory *factory = AUR_RECEIVER_INGEST_FACTORY (mf);
  AurReceiverIngestMedia *media;
  GstRTSPMedia *rtspmedia;
  GstElement *element, *pipeline;
  GstElement *decodebin;

  element = create_element (mf, url);
  if (element == NULL)
    goto no_element;

  /* create a new empty media */
  rtspmedia =
      g_object_new (AUR_TYPE_RECEIVER_INGEST_MEDIA, "element", element, NULL);

  gst_rtsp_media_collect_streams (rtspmedia);

  pipeline = gst_pipeline_new ("media-pipeline");
  gst_pipeline_use_clock (GST_PIPELINE (pipeline), gst_system_clock_obtain ());
  gst_rtsp_media_take_pipeline (rtspmedia, GST_PIPELINE_CAST (pipeline));

  media = AUR_RECEIVER_INGEST_MEDIA (rtspmedia);
  media->bin = gst_object_ref (element);
  media->id = factory->id;
  media->processor = g_object_ref (factory->processor);

  decodebin = gst_bin_get_by_name (media->bin, "depay0");
  g_signal_connect (G_OBJECT (decodebin), "pad-added",
      (GCallback) dec_pad_added_cb, media);
  gst_object_unref ((GstObject *) decodebin);

  return rtspmedia;

  /* ERRORS */
no_element:
  {
    g_critical ("could not create element");
    return NULL;
  }
}

static GstElement *
create_element (GstRTSPMediaFactory * mf G_GNUC_UNUSED,
    const GstRTSPUrl * url G_GNUC_UNUSED)
{
  const gchar *launch_line = "( decodebin name=depay0 appsink name=chan0 )";
  GstElement *element;
  GError *error = NULL;

  /* parse the user provided launch line */
  element = gst_parse_launch (launch_line, &error);
  if (element == NULL)
    goto parse_error;

  if (error != NULL) {
    /* a recoverable error was encountered */
    GST_WARNING ("recoverable parsing error: %s", error->message);
    g_error_free (error);
  }

  return element;
parse_error:
  {
    g_critical ("could not parse launch syntax (%s): %s", launch_line,
        (error ? error->message : "unknown reason"));
    if (error)
      g_error_free (error);
    return NULL;
  }
}

G_DEFINE_TYPE (AurReceiverIngestMedia, aur_receiver_ingest_media,
    GST_TYPE_RTSP_MEDIA);

static void aur_receiver_ingest_media_dispose (GObject * object);

static void
aur_receiver_ingest_media_class_init (AurReceiverIngestMediaClass * media_klass)
{
  GObjectClass *object_class = (GObjectClass *) (media_klass);
  GstRTSPMediaClass *klass = (GstRTSPMediaClass *) (media_klass);
  klass->setup_rtpbin = custom_setup_rtpbin;

  object_class->dispose = aur_receiver_ingest_media_dispose;
}

static void
aur_receiver_ingest_media_init (AurReceiverIngestMedia * media G_GNUC_UNUSED)
{
}

static void
aur_receiver_ingest_media_dispose (GObject * object)
{
  AurReceiverIngestMedia *media = AUR_RECEIVER_INGEST_MEDIA (object);

  if (media->bin) {
    gst_object_unref (media->bin);
    media->bin = NULL;
  }

  if (media->processor) {
    g_object_unref (media->processor);
    media->processor = NULL;
  }
}

static gboolean
custom_setup_rtpbin (GstRTSPMedia * media G_GNUC_UNUSED, GstElement * rtpbin)
{
  g_object_set (rtpbin, "ntp-time-source", 3, NULL);
  return TRUE;
}

GstRTSPMediaFactory *
aur_receiver_ingest_factory_new (guint id, AurReceiverProcessor * processor)
{
  AurReceiverIngestFactory *result;

  result = g_object_new (AUR_TYPE_RECEIVER_INGEST_FACTORY, NULL);
  /* FIXME: Set these via a property */
  result->id = id;
  result->processor = g_object_ref (processor);

  return (GstRTSPMediaFactory *) result;
}
