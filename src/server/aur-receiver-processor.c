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
 * Aurena Receiver Processor receives incoming audio feeds
 * from Aurena Receiver Ingest objects, mixes them and
 * outputs the 8-channel stream for audition/triangulation
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/audio/audio-info.h>

#include "aur-receiver-processor.h"

GST_DEBUG_CATEGORY_STATIC (process_debug);
#define GST_CAT_DEFAULT process_debug

static void aur_receiver_processor_constructed (GObject * object);
static void aur_receiver_processor_dispose (GObject * object);
static void aur_receiver_processor_finalize (GObject * object);
static void aur_receiver_processor_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void aur_receiver_processor_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void aur_receiver_processor_init_analyser (AurReceiverProcessor * processor);
static void aur_receiver_processor_cleanup (AurReceiverProcessor * processor);

#define SAMPLING_RATE 48000
#define SAMPLES_PER_FRAME 512
#define NB_MICROPHONES 8

struct _AurReceiverProcessorPrivate
{
  GstBuffer *cache;

  gboolean analyser;

  float deinterleave_buffer[NB_MICROPHONES][SAMPLES_PER_FRAME];
};

struct _AurReceiverProcessorChannel
{
  gint id;
  gboolean inuse;
  GstAppSrc *appsrc;
};

enum
{
  PROP_0,
  PROP_LAST
};

static void
_do_init ()
{
  GST_DEBUG_CATEGORY_INIT (process_debug, "aurena/processor", 0,
      "Aurena Audio Receiver Processor debug");
}

G_DEFINE_TYPE_WITH_CODE (AurReceiverProcessor, aur_receiver_processor,
    G_TYPE_OBJECT, _do_init ());

static void
aur_receiver_processor_class_init (AurReceiverProcessorClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) (klass);

  object_class->constructed = aur_receiver_processor_constructed;
  object_class->set_property = aur_receiver_processor_set_property;
  object_class->get_property = aur_receiver_processor_get_property;
  object_class->dispose = aur_receiver_processor_dispose;
  object_class->finalize = aur_receiver_processor_finalize;

  g_type_class_add_private (klass, sizeof (AurReceiverProcessorPrivate));
}

static GstFlowReturn
processor_handle_new_sample (GstAppSink * appsink, gpointer user_data);

static GstAppSinkCallbacks sink_cb = {
  NULL, NULL,
  processor_handle_new_sample,
  {0,}
};

static void
aur_receiver_processor_init (AurReceiverProcessor * processor)
{
  GstElement *interleave, *out;
  gint i;

  processor->priv = G_TYPE_INSTANCE_GET_PRIVATE (G_OBJECT (processor),
      AUR_TYPE_RECEIVER_PROCESSOR, AurReceiverProcessorPrivate);

  /* Can't actually handle anything other than 8 mics, despite the
   * configurable-looking #define */
  g_assert (NB_MICROPHONES == 8);

  processor->pipeline =
      gst_parse_launch
      ("audiointerleave name=i ! tee name=t ! wavenc ! filesink name=fsink async=false t. ! audioconvert ! audio/x-raw,channels=8,format=F32LE ! appsink name=out async=false sync=false",
      NULL);
  gst_pipeline_use_clock (GST_PIPELINE (processor->pipeline),
      gst_system_clock_obtain ());

  interleave = gst_bin_get_by_name (GST_BIN (processor->pipeline), "i");
  processor->filesink =
      gst_bin_get_by_name (GST_BIN (processor->pipeline), "fsink");

  out = gst_bin_get_by_name (GST_BIN (processor->pipeline), "out");
  gst_app_sink_set_callbacks (GST_APP_SINK_CAST (out),
    &sink_cb, processor, NULL);

  g_object_set (interleave, "alignment-threshold", 1 * GST_MSECOND,
      "latency", 60 * GST_MSECOND, NULL);

  for (i = 0; i < NB_MICROPHONES; i++) {
    AurReceiverProcessorChannel *channel;
    GstElement *chain =
        gst_parse_bin_from_description ("appsrc name=src ! audioconvert", TRUE,
        NULL);
    GstPad *src, *sink;
    gchar *padname;

    src = gst_element_get_static_pad (chain, "src");

    padname = g_strdup_printf ("sink_%u", i);
    sink = gst_element_get_request_pad (interleave, padname);
    g_free (padname);

    gst_bin_add (GST_BIN (processor->pipeline), chain);
    gst_pad_link (src, sink);

    channel = processor->channels[i] = g_new0 (AurReceiverProcessorChannel, 1);
    channel->id = i;
    channel->appsrc =
        GST_APP_SRC (gst_bin_get_by_name (GST_BIN (chain), "src"));
    g_object_set (channel->appsrc, "is-live", TRUE, "format", GST_FORMAT_TIME,
        NULL);
  }

  gst_object_unref (interleave);
}

static void
aur_receiver_processor_constructed (GObject * object)
{
  if (G_OBJECT_CLASS (aur_receiver_processor_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (aur_receiver_processor_parent_class)->constructed (object);
}

static void
aur_receiver_processor_dispose (GObject * object)
{
  AurReceiverProcessor *processor = (AurReceiverProcessor *) (object);
  if (processor->filesink) {
    gst_object_unref (processor->filesink);
    processor->filesink = NULL;
  }

  if (processor->pipeline) {
    gst_element_set_state (processor->pipeline, GST_STATE_NULL);
    gst_object_unref (processor->pipeline);
    processor->pipeline = NULL;
  }

  G_OBJECT_CLASS (aur_receiver_processor_parent_class)->dispose (object);
}

static void
aur_receiver_processor_finalize (GObject * object)
{
  AurReceiverProcessor *processor = (AurReceiverProcessor *) (object);
  AurReceiverProcessorPrivate *priv = processor->priv;
  gint i;

  for (i = 0; i < NB_MICROPHONES; i++) {
    AurReceiverProcessorChannel *channel = NULL;

    channel = processor->channels[i];
    gst_object_unref (channel->appsrc);
    g_free (channel);
  }

  aur_receiver_processor_cleanup(processor);

  gst_buffer_replace (&priv->cache, NULL);

  G_OBJECT_CLASS (aur_receiver_processor_parent_class)->finalize (object);
}

static void
aur_receiver_processor_set_property (GObject * object, guint prop_id,
    const GValue * value G_GNUC_UNUSED, GParamSpec * pspec)
{
  //AurReceiverProcessor *processor = (AurReceiverProcessor *) (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
aur_receiver_processor_get_property (GObject * object, guint prop_id,
    GValue * value G_GNUC_UNUSED, GParamSpec * pspec)
{
  //AurReceiverProcessor *processor = (AurReceiverProcessor *) (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

AurReceiverProcessorChannel *
aur_receiver_processor_get_channel (AurReceiverProcessor * processor)
{
  AurReceiverProcessorChannel *channel = NULL;
  gint i;

  for (i = 0; i < NB_MICROPHONES; i++) {
    AurReceiverProcessorChannel *cur = processor->channels[i];
    if (cur->inuse)
      continue;
    channel = cur;
    channel->inuse = TRUE;
    processor->n_inuse++;
    GST_DEBUG_OBJECT (processor, "Allocated new channel %d", i);
    break;
  }

  return channel;
}

void
aur_receiver_processor_push_sample (AurReceiverProcessor * processor,
    AurReceiverProcessorChannel * channel, GstSample * sample,
    GstClockTime play_time)
{
  GstClockTime base_time;
  GstBuffer *buf;
  GstSample *push_sample;

  GST_DEBUG_OBJECT (processor, "Got sample %" GST_PTR_FORMAT " on channel %d",
      sample, channel->id);

  if (processor->state != GST_STATE_PLAYING) {
    GST_DEBUG_OBJECT (processor, "Got first sample. Starting output");
    if (processor->filesink) {
      GDateTime *now = g_date_time_new_now_utc ();
      gchar *nowstr = g_date_time_format (now, "%Y-%m-%d-%H:%M:%S");
      gchar *fname = g_strdup_printf ("recordings/capture-%s.wav", nowstr);
      g_object_set (G_OBJECT (processor->filesink), "location", fname, NULL);

      g_date_time_unref (now);
      g_free (nowstr);
      g_free (fname);
    }
    aur_receiver_processor_init_analyser (processor);
    processor->state = GST_STATE_PLAYING;
    gst_element_set_state (processor->pipeline, processor->state);

    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN (processor->pipeline),
        GST_DEBUG_GRAPH_SHOW_ALL, "processor");

  }

  /* Adjust passed play_time (time on the clock) back into our segment */
  buf = gst_sample_get_buffer (sample);
  buf = gst_buffer_make_writable (gst_buffer_ref (buf));

  base_time = gst_element_get_base_time (GST_ELEMENT (channel->appsrc));
  play_time = (play_time >= base_time) ? play_time - base_time : 0;

  GST_DEBUG_OBJECT (processor,
      "Adjusting buffer PTS %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
      " based on base time %" GST_TIME_FORMAT " and play time %"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_PTS (buf)),
      GST_TIME_ARGS (play_time), GST_TIME_ARGS (base_time),
      GST_TIME_ARGS (play_time));

  GST_BUFFER_PTS (buf) = GST_BUFFER_DTS (buf) = play_time;

  push_sample = gst_sample_new (buf, gst_sample_get_caps (sample), NULL, NULL);
  gst_sample_unref (sample);
  gst_buffer_unref (buf);

  /* FIXME: Handle errors */
  if (gst_app_src_push_sample (channel->appsrc, push_sample) != GST_FLOW_OK)
    g_print ("Failed to push sample on channel %d\n", channel->id);

  /* Push silence buffers into unused channels */
  if (channel->id == 0) {
    GstAudioInfo info;
    gint i;
    GstSample *silence_sample = NULL;

    for (i = 1; i < NB_MICROPHONES; i++) {
      AurReceiverProcessorChannel *cur = processor->channels[i];
      if (cur->inuse == 0) {
        if (silence_sample == NULL) {
          GstBuffer *silence;
          GstMapInfo map_info;

          gst_audio_info_from_caps (&info, gst_sample_get_caps (push_sample));
          silence = gst_buffer_copy (gst_sample_get_buffer (push_sample));
          gst_buffer_map (silence, &map_info, GST_MAP_WRITE);
          gst_audio_format_fill_silence (info.finfo, map_info.data,
              map_info.size);
          gst_buffer_unmap (silence, &map_info);
          silence_sample =
              gst_sample_new (silence, gst_sample_get_caps (push_sample), NULL,
              NULL);
          gst_buffer_unref (silence);
        }

        GST_DEBUG_OBJECT (processor, "Pushing silence on unused channel %d", i);
        if (gst_app_src_push_sample (cur->appsrc,
                silence_sample) != GST_FLOW_OK)
          g_print ("Failed to push sample on channel %d\n", cur->id);
      }
    }
    if (silence_sample != NULL)
      gst_sample_unref (silence_sample);
  }
  gst_sample_unref (push_sample);
}

static void
end_recording (AurReceiverProcessor * processor)
{
  GstEvent *eos = gst_event_new_eos ();
  GstBus *bus;
  GstMessage *msg;

  bus = gst_element_get_bus (GST_ELEMENT (processor->pipeline));
  gst_element_send_event (GST_ELEMENT (processor->pipeline), eos);
  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    gchar *debug;
    GError *err;
    gst_message_parse_error (msg, &err, &debug);

    g_printerr ("Failed to stop recording: %s\n", (debug) ? debug : "none");
    g_free (debug);
    g_print ("Error: %s\n", err->message);
    g_error_free (err);
  } else {
    GST_LOG_OBJECT (processor, "Closed recording");
  }

  gst_message_unref (msg);
}

void
aur_receiver_processor_release_channel (AurReceiverProcessor * processor,
    AurReceiverProcessorChannel * channel)
{
  GST_DEBUG_OBJECT (processor, "Releasing channel %d", channel->id);
  g_assert (processor->n_inuse > 0);

  channel->inuse = FALSE;
  processor->n_inuse--;

  if (processor->n_inuse == 0) {
    GST_DEBUG_OBJECT (processor, "No more channels. Stopping output");
    if (processor->filesink)
      end_recording (processor);
    processor->state = GST_STATE_NULL;
    gst_element_set_state (processor->pipeline, processor->state);
    aur_receiver_processor_cleanup (processor);
  }
}

AurReceiverProcessor *
aur_receiver_processor_new ()
{
  return g_object_new (AUR_TYPE_RECEIVER_PROCESSOR, NULL);
}

static gboolean
processFrame (AurReceiverProcessor * processor,
    float *samples, unsigned int n_frames)
{
  AurReceiverProcessorPrivate *priv = processor->priv;
  guint channel, frame_index;

  g_return_val_if_fail (priv->analyser, FALSE);

  //#1 - Let's create the float data for processing
  for (channel = 0; channel < NB_MICROPHONES; channel++) {
    for (frame_index = 0; frame_index < n_frames; frame_index++) {
      priv->deinterleave_buffer[channel][frame_index] =
          samples[channel + (NB_MICROPHONES * frame_index)];
    }
  }

  return TRUE;
}

static GstFlowReturn
processor_handle_new_sample (GstAppSink * appsink, gpointer user_data)
{
  AurReceiverProcessor * processor = (AurReceiverProcessor *)(user_data);
  AurReceiverProcessorPrivate *priv = processor->priv;
  GstSample *sample;
  GstBuffer *buf;
  GstMapInfo info;
  guint frames_avail, frame_offset;
  gfloat *samples;

  sample = gst_app_sink_pull_sample (appsink);
  if (!sample)
    return GST_FLOW_OK;

  buf = gst_buffer_ref (gst_sample_get_buffer (sample));

  if (priv->cache) {
    GST_DEBUG_OBJECT (processor, "Appending new buffer to cached samples");
    buf = gst_buffer_append (priv->cache, buf);
    priv->cache = NULL;
  }

  if (!gst_buffer_map (buf, &info, GST_MAP_READ)) {
    GST_WARNING_OBJECT (processor, "Failed to map sample buffer");
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }

  frames_avail = info.size / (sizeof (float) * NB_MICROPHONES);
  frame_offset = 0;
  samples = (gfloat *) info.data;

  while (frames_avail >= SAMPLES_PER_FRAME) {
    processFrame (processor, samples + (NB_MICROPHONES * frame_offset),
       SAMPLES_PER_FRAME);
    frame_offset += SAMPLES_PER_FRAME;
    frames_avail -= SAMPLES_PER_FRAME;
  } 

  if (frames_avail > 0) {
    GST_DEBUG_OBJECT (processor, "Storing %d frames for next iteration",
        frames_avail);
    priv->cache = gst_buffer_copy_region (buf, GST_BUFFER_COPY_MEMORY,
        frame_offset * sizeof(float) * NB_MICROPHONES,
        frames_avail * sizeof(float) * NB_MICROPHONES);
  }

  gst_buffer_unref (buf);
  return GST_FLOW_OK;
}

static void
aur_receiver_processor_init_analyser (AurReceiverProcessor * processor)
{
  AurReceiverProcessorPrivate *priv = processor->priv;

  priv->analyser = TRUE;

  GST_DEBUG_OBJECT (processor, "Created analyser");
}

static void
aur_receiver_processor_cleanup (AurReceiverProcessor * processor)
{
  AurReceiverProcessorPrivate *priv = processor->priv;

  gst_buffer_replace (&priv->cache, NULL);

  if (!priv->analyser)
    return;

  priv->analyser = FALSE;

  GST_DEBUG_OBJECT (processor, "Destroyed analyser");
}
