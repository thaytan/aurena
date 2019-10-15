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
 * SECTION:element-quietdec
 *
 * FIXME:Describe quietdec here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! quietdec ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/audio/audio.h>

#include "gstquietdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_quiet_dec_debug);
#define GST_CAT_DEFAULT gst_quiet_dec_debug

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
    GST_STATIC_CAPS (GST_AUDIO_CAPS_MAKE (GST_AUDIO_NE (F32)))
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/x-raw")
    );

#define gst_quiet_dec_parent_class parent_class
G_DEFINE_TYPE (GstQuietDec, gst_quiet_dec, GST_TYPE_AUDIO_ENCODER);

static void gst_quiet_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_quiet_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_quiet_dec_start (GstAudioEncoder * enc);
static gboolean gst_quiet_dec_stop (GstAudioEncoder * enc);
static gboolean gst_quiet_dec_set_format (GstAudioEncoder * enc,
    GstAudioInfo * info);
static GstFlowReturn gst_quiet_dec_handle_frame (GstAudioEncoder * enc,
    GstBuffer * in_buf);
static void gst_quiet_dec_flush (GstAudioEncoder * vorbisenc);
static GstCaps *gst_quiet_dec_sink_getcaps (GstAudioEncoder *enc, GstCaps *filter);

static void
gst_quiet_dec_class_init (GstQuietDecClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstAudioEncoderClass *base_class = (GstAudioEncoderClass *) (klass);

  gobject_class->set_property = gst_quiet_dec_set_property;
  gobject_class->get_property = gst_quiet_dec_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PROFILES,
      g_param_spec_string ("profiles-file", "profiles",
          "Path to profiles.json file", DEFAULT_PROFILES_PATH,
          G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PROFILE,
      g_param_spec_string ("profile", "profile",
          "Modem profile to use", DEFAULT_PROFILE,
          G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_details_simple (gstelement_class,
      "QuietDec",
      "Codec/Audio/Demodulator",
      "Quiet MODEM demodulator", "Jan Schmidt <jan@centricular.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  base_class->start = GST_DEBUG_FUNCPTR (gst_quiet_dec_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_quiet_dec_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_quiet_dec_set_format);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_quiet_dec_handle_frame);
  base_class->flush = GST_DEBUG_FUNCPTR (gst_quiet_dec_flush);
  base_class->getcaps = gst_quiet_dec_sink_getcaps;
}

static void
gst_quiet_dec_init (GstQuietDec * filter)
{

  filter->profiles_path = g_strdup (DEFAULT_PROFILES_PATH);
  filter->profile = g_strdup (DEFAULT_PROFILE);
}

static void
gst_quiet_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQuietDec *filter = GST_QUIETDEC (object);

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
gst_quiet_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstQuietDec *filter = GST_QUIETDEC (object);

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
gst_quiet_dec_start (GstAudioEncoder *base)
{
  return TRUE;
}

static gboolean
gst_quiet_dec_stop (GstAudioEncoder *base)
{
  GstQuietDec *dec = GST_QUIETDEC (base);

  if (dec->quiet) {
    quiet_decoder_destroy(dec->quiet);
    dec->quiet = NULL;
  }

  if (dec->quietopt) {
    free (dec->quietopt);
    dec->quietopt = NULL;
  }

  return TRUE;
}

static gboolean
gst_quiet_dec_set_format (GstAudioEncoder *base, GstAudioInfo * info)
{
  GstQuietDec *dec = GST_QUIETDEC (base);
  GstCaps *caps = gst_pad_get_pad_template_caps (base->srcpad);
  gint sample_rate;

  /* FIXME: Support non-mono input */
  if (GST_AUDIO_INFO_CHANNELS (info) != 1) {
    GST_ELEMENT_ERROR (GST_ELEMENT (base), STREAM, DECODE,
        ("%d channels not supported", GST_AUDIO_INFO_CHANNELS (info)), (NULL));
    return FALSE;
  }

  GST_DEBUG_OBJECT (dec, "Setting output caps: %" GST_PTR_FORMAT, caps);
  gst_audio_encoder_set_output_format (base, caps);
  gst_caps_unref (caps);

  GST_OBJECT_LOCK (base);
  dec->quietopt = quiet_decoder_profile_filename(dec->profiles_path, dec->profile);
  GST_OBJECT_UNLOCK (base);

  if (dec->quietopt == NULL) {
    GST_ELEMENT_ERROR (GST_ELEMENT (base), STREAM, DECODE,
        ("Could not load modem profile"), (NULL));
    return FALSE;
  }

  sample_rate = GST_AUDIO_INFO_RATE (info);
  dec->quiet = quiet_decoder_create(dec->quietopt, sample_rate);

  return TRUE;
}

static GstFlowReturn recv_all (GstQuietDec *dec)
{
  GstFlowReturn ret = GST_FLOW_OK;
  ssize_t read_bytes;
  size_t buflen = 4096;
  GstBuffer *outbuf;
  GstMapInfo outmap;

  read_bytes = buflen;
  while (read_bytes == buflen) {
    outbuf = gst_audio_encoder_allocate_output_buffer (GST_AUDIO_ENCODER (dec), buflen);

    gst_buffer_map (outbuf, &outmap, GST_MAP_WRITE);
    read_bytes = quiet_decoder_recv(dec->quiet, outmap.data, outmap.size);
    gst_buffer_unmap (outbuf, &outmap);

    if (read_bytes < 0) {
      gst_buffer_unref (outbuf);
      return GST_FLOW_OK;
    }
    gst_buffer_set_size (outbuf, read_bytes);
    ret = gst_audio_encoder_finish_frame (GST_AUDIO_ENCODER (dec), outbuf,
              read_bytes / sizeof (quiet_sample_t));
    if (ret != GST_FLOW_OK)
      return ret;
  }

  return ret;
}

static GstFlowReturn
gst_quiet_dec_handle_frame (GstAudioEncoder *base, GstBuffer *in_buf)
{
  GstQuietDec *dec = GST_QUIETDEC (base);
  GstFlowReturn result;
  GstMapInfo map;
  size_t avail;

  /* Draining */
  if (G_UNLIKELY (!in_buf))
   return GST_FLOW_OK;

  if (dec->quiet == NULL) {
    GST_ERROR_OBJECT (base, "Quiet demodulator not initialised");
    return GST_FLOW_ERROR;
  }

  gst_buffer_map (in_buf, &map, GST_MAP_READ);
  avail = map.size / sizeof (quiet_sample_t);
  quiet_decoder_consume(dec->quiet, (quiet_sample_t *) map.data, avail);
  gst_buffer_unmap (in_buf, &map);

  result = recv_all(dec);
  if (result == GST_FLOW_OK) {
    quiet_decoder_flush(dec->quiet);
    result = recv_all(dec);
  }

  return result;
}

static void
gst_quiet_dec_flush (GstAudioEncoder * quietdec)
{
}

static GstCaps *
gst_quiet_dec_sink_getcaps (GstAudioEncoder *base, GstCaps *filter)
{
  GstCaps *caps = gst_pad_get_pad_template_caps (base->sinkpad);
  caps = gst_caps_make_writable (caps);

  gst_caps_set_simple (caps, "channels", G_TYPE_INT, 1, NULL);

  return caps;
}

gboolean
quietdec_init (GstPlugin * quietdec)
{
  GST_DEBUG_CATEGORY_INIT (gst_quiet_dec_debug, "quietdec",
      0, "Quiet MODEM decoder");

  return gst_element_register (quietdec, "quietdec", GST_RANK_NONE,
      GST_TYPE_QUIETDEC);
}
