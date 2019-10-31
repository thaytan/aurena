/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2019 Jan Schmidt <jan@centricular.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

/**
 * SECTION:element-quietenc
 *
 * FIXME:Describe quietenc here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! quietenc ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/audio/audio.h>

#include "gstquietenc.h"

GST_DEBUG_CATEGORY_STATIC (gst_quiet_enc_debug);
#define GST_CAT_DEFAULT gst_quiet_enc_debug

enum
{
  PROP_0,
  PROP_PROFILES,
  PROP_PROFILE
};

#define DEFAULT_PROFILES_PATH QUIET_PROFILES_PATH
#define DEFAULT_PROFILE "audible"

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/x-raw")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_CAPS_MAKE (GST_AUDIO_NE (F32)))
    );

#define gst_quiet_enc_parent_class parent_class
G_DEFINE_TYPE (GstQuietEnc, gst_quiet_enc, GST_TYPE_AUDIO_DECODER);

static void gst_quiet_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_quiet_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean quiet_enc_start (GstAudioDecoder * dec);
static gboolean quiet_enc_stop (GstAudioDecoder * dec);
static GstFlowReturn quiet_enc_handle_frame (GstAudioDecoder * dec,
    GstBuffer * buffer);
static void quiet_enc_flush (GstAudioDecoder * dec, gboolean hard);
static gboolean quiet_enc_set_format (GstAudioDecoder *base, GstCaps * caps);
static GstCaps *quiet_sink_getcaps (GstAudioDecoder *base, GstCaps * filter);

static void
gst_quiet_enc_class_init (GstQuietEncClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstAudioDecoderClass *base_class = (GstAudioDecoderClass *) (klass);

  gobject_class->set_property = gst_quiet_enc_set_property;
  gobject_class->get_property = gst_quiet_enc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PROFILES,
      g_param_spec_string ("profiles-file", "profiles",
          "Path to profiles.json file", DEFAULT_PROFILES_PATH,
          G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PROFILE,
      g_param_spec_string ("profile", "profile",
          "Modem profile to use", DEFAULT_PROFILE,
          G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_details_simple (gstelement_class,
      "QuietEnc",
      "Codec/Audio/Modulator",
      "Quiet MODEM modulator", "Jan Schmidt <jan@centricular.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  base_class->start = quiet_enc_start;
  base_class->stop = quiet_enc_stop;
  base_class->set_format = quiet_enc_set_format;
  base_class->handle_frame = quiet_enc_handle_frame;
  base_class->flush = quiet_enc_flush;
  base_class->getcaps = quiet_sink_getcaps;

}

static void
gst_quiet_enc_init (GstQuietEnc * enc)
{
  gst_audio_decoder_set_use_default_pad_acceptcaps (GST_AUDIO_DECODER_CAST(enc), TRUE);
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_AUDIO_DECODER_SINK_PAD (enc));

  enc->profiles_path = g_strdup (DEFAULT_PROFILES_PATH);
  enc->profile = g_strdup (DEFAULT_PROFILE);
}

static void
gst_quiet_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQuietEnc *filter = GST_QUIETENC (object);

  switch (prop_id) {
    case PROP_PROFILES:
      GST_OBJECT_LOCK (filter);
      g_free (filter->profiles_path);
      filter->profiles_path = g_value_dup_string (value);
      GST_OBJECT_UNLOCK (filter);
      break;
    case PROP_PROFILE:
      GST_OBJECT_LOCK (filter);
      g_free (filter->profile);
      filter->profile = g_value_dup_string (value);
      GST_OBJECT_UNLOCK (filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_quiet_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstQuietEnc *filter = GST_QUIETENC (object);

  switch (prop_id) {
    case PROP_PROFILES:
      GST_OBJECT_LOCK (filter);
      g_value_set_string (value, filter->profiles_path);
      GST_OBJECT_UNLOCK (filter);
      break;
    case PROP_PROFILE:
      GST_OBJECT_LOCK (filter);
      g_value_set_string (value, filter->profile);
      GST_OBJECT_UNLOCK (filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
quiet_enc_start (GstAudioDecoder *base)
{
  GstQuietEnc *enc = GST_QUIETENC (base);

  GST_OBJECT_LOCK (base);
  enc->quietopt = quiet_encoder_profile_filename(enc->profiles_path, enc->profile);
  GST_OBJECT_UNLOCK (base);

  if (enc->quietopt == NULL) {
    GST_ELEMENT_ERROR (GST_ELEMENT (base), STREAM, ENCODE,
        ("Could not load modem profile '%s'", enc->profile), (NULL));
    return FALSE;
  }

  return TRUE;
}

static gboolean
quiet_enc_stop (GstAudioDecoder *base)
{
  GstQuietEnc *enc = GST_QUIETENC (base);

  if (enc->quiet) {
    quiet_encoder_destroy(enc->quiet);
    enc->quiet = NULL;
  }

  if (enc->quietopt) {
    free (enc->quietopt);
    enc->quietopt = NULL;
  }

  return TRUE;
}

static GstFlowReturn
quiet_enc_handle_frame (GstAudioDecoder *base, GstBuffer * buffer)
{
  GstQuietEnc *enc = GST_QUIETENC (base);
  GstFlowReturn result;

  GstBuffer *cur = NULL, *outbuf = NULL;
  GstMapInfo map, outmap;
  size_t frame_len, avail;
  size_t samplebuf_len = 16384;
  size_t i;

  if (enc->quiet == NULL)
    goto no_format;

  /* Draining */
  if (G_UNLIKELY (!buffer))
   return GST_FLOW_OK;

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  frame_len = quiet_encoder_get_frame_len(enc->quiet);
  avail = map.size;

  for (i = 0; i < avail; i += frame_len) {
    size_t remain = avail-i; 
    frame_len = (frame_len > remain) ? remain : frame_len;
    GST_LOG_OBJECT (enc, "Sending %zu bytes to Quiet encoder", frame_len);
    quiet_encoder_send(enc->quiet, map.data + i, frame_len);
  }
  gst_buffer_unmap (buffer, &map);

  ssize_t written = samplebuf_len;
  while (written == samplebuf_len) {
    cur = gst_audio_decoder_allocate_output_buffer (base, samplebuf_len * sizeof(quiet_sample_t));
    gst_buffer_map (cur, &outmap, GST_MAP_WRITE);
    written = quiet_encoder_emit(enc->quiet, (quiet_sample_t *) outmap.data, samplebuf_len);
    gst_buffer_unmap (cur, &outmap);

    if (written > 0) {
      GST_LOG_OBJECT (enc, "Read %zu samples from Quiet encoder", written);
      gst_buffer_set_size (cur, written * sizeof(quiet_sample_t));  
      if (outbuf)
        outbuf = gst_buffer_append (outbuf, cur);
      else
        outbuf = cur;
    }
    else {
      gst_buffer_unref (cur);
    }
  }
  if (outbuf)
    gst_buffer_copy_into (outbuf, buffer, GST_BUFFER_COPY_METADATA, 0, -1);
  result = gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (base), outbuf, 1);

  return result;
no_format:
  GST_ELEMENT_ERROR (GST_ELEMENT (base), STREAM, ENCODE,
      ("Buffer received before input caps"), (NULL));
  return GST_FLOW_ERROR;
}

static void
quiet_enc_flush (GstAudioDecoder * dec, gboolean hard)
{
}

static gboolean
quiet_enc_set_format (GstAudioDecoder *base, GstCaps * caps)
{
  GstQuietEnc *enc = GST_QUIETENC (base);
  GstAudioInfo info;
  GstAudioChannelPosition chanpos = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;

  /* (Re)Create encoder */
  if (enc->quiet) {
    free (enc->quiet);
    enc->quiet = NULL;
  }
  enc->quiet = quiet_encoder_create(enc->quietopt, SAMPLE_RATE);
  if (enc->quiet == NULL) {
    GST_ELEMENT_ERROR (GST_ELEMENT (base), STREAM, ENCODE,
        ("Failed to create Quiet encoder"), (NULL));
    return FALSE;
  }

  gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_F32, SAMPLE_RATE, 1, &chanpos);
  gst_audio_decoder_set_output_format (base, &info);

  return TRUE;
}

static GstCaps *quiet_sink_getcaps (GstAudioDecoder *base, GstCaps *filter)
{
  GstCaps *caps = gst_pad_get_pad_template_caps (base->sinkpad);

  return caps;
}

gboolean
quietenc_init (GstPlugin * quietenc)
{
  GST_DEBUG_CATEGORY_INIT (gst_quiet_enc_debug, "quietenc",
      0, "Quiet MODEM encoder");

  return gst_element_register (quietenc, "quietenc", GST_RANK_NONE,
      GST_TYPE_QUIETENC);
}
