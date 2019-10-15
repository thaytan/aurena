/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2019 Jan Schmidt <thaytan@noraisin.net>
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
 * SECTION:element-odas
 *
 * FIXME:Describe odas here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! odas ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/audio/audio.h>

#include "gstodas.h"

GST_DEBUG_CATEGORY_STATIC (gst_odas_debug);
#define GST_CAT_DEFAULT gst_odas_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_CONFIG_FILE
};

#define DEFAULT_PROP_CONFIG_FILE NULL

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_CAPS_MAKE (GST_AUDIO_NE (S16)))
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_CAPS_MAKE (GST_AUDIO_NE (S16)))
    );

#define gst_odas_parent_class parent_class
G_DEFINE_TYPE (GstODAS, gst_odas, GST_TYPE_ELEMENT);

static void gst_odas_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_odas_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_odas_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_odas_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);

/* GObject vmethod implementations */

/* initialize the odas's class */
static void
gst_odas_class_init (GstODASClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_odas_set_property;
  gobject_class->get_property = gst_odas_get_property;

  g_object_class_install_property (gobject_class, PROP_CONFIG_FILE,
      g_param_spec_string ("config-file", "Configuration File",
          "Path to configuration file", DEFAULT_PROP_CONFIG_FILE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_details_simple (gstelement_class,
      "ODAS",
      "Filter/Analyzer/Audio",
      "ODAS audio analysis filter",
      "Jan Schmidt <thaytan@noraisin.net>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_odas_init (GstODAS * filter)
{
  filter->config_file = DEFAULT_PROP_CONFIG_FILE;

  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_event_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_odas_sink_event));
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_odas_chain));
  GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS (filter->srcpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
}

static void
gst_odas_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstODAS *odas = GST_ODAS (object);

  switch (prop_id) {
    case PROP_CONFIG_FILE:
      GST_OBJECT_LOCK (odas);
      g_free (odas->config_file);
      odas->config_file = g_value_dup_string (value);
      GST_OBJECT_UNLOCK (odas);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_odas_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstODAS *odas = GST_ODAS (object);

  switch (prop_id) {
    case PROP_CONFIG_FILE:
      GST_OBJECT_LOCK (odas);
      g_value_set_string (value, odas->config_file);
      GST_OBJECT_UNLOCK (odas);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* this function handles sink events */
static gboolean
gst_odas_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstODAS *filter;
  gboolean ret;

  filter = GST_ODAS (parent);

  GST_LOG_OBJECT (filter, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      /* do something with the caps */

      /* and forward */
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_odas_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstODAS *odas = GST_ODAS (parent);

  if (odas->odas_initted == FALSE) {
    GST_OBJECT_LOCK (odas);
    if (odas->config_file == NULL) {
      GST_OBJECT_UNLOCK (odas);
      goto no_config_file;
    }
    odas_configs_construct (&odas->odas_cfgs, odas->config_file);
    GST_OBJECT_UNLOCK (odas);

    odas_objects_construct (&odas->odas_objs, &odas->odas_cfgs);
    odas->odas_initted = TRUE;
  }

  /* just push out the incoming buffer without touching it */
  return gst_pad_push (odas->srcpad, buf);

no_config_file:
  gst_buffer_unref (buf);
  GST_ELEMENT_ERROR (odas, STREAM, DECODE, ("No configuration file provided"),
      (NULL));
  return GST_FLOW_ERROR;
}

static gboolean
odas_init (GstPlugin * odas)
{
  GST_DEBUG_CATEGORY_INIT (gst_odas_debug, "odas", 0, "Aurena ODAS plugin");
  return gst_element_register (odas, "odas", GST_RANK_NONE, GST_TYPE_ODAS);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    odas,
    "Aurena ODAS plugin",
    odas_init, PACKAGE_VERSION, GST_LICENSE, PACKAGE_NAME, PACKAGE_URL)
