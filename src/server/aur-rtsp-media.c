/* GStreamer
 * Copyright (C) 2012-2015 Jan Schmidt <thaytan@noraisin.net>
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
 * Aurena Manager is the central object which:
 *   creates the network clock
 *   Establishes libsoup session
 *   Creates RTSP sessions as needed
 *   Distributes the network clock and base time to clients
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "aur-rtsp-media.h"

static GstElement *create_pipeline (GstRTSPMediaFactory * factory,
    GstRTSPMedia * media);

GstRTSPMediaFactoryURI *
aur_rtsp_media_factory_uri_new (GstClock *c)
{
  AurRTSPMediaFactoryURI *result;

  result = g_object_new (AUR_TYPE_RTSP_MEDIA_FACTORY_URI, NULL);
  result->netclock = c;

  return (GstRTSPMediaFactoryURI *) result;
}

G_DEFINE_TYPE (AurRTSPMediaFactoryURI, aur_rtsp_media_factory_uri,
    GST_TYPE_RTSP_MEDIA_FACTORY_URI);

static gboolean custom_setup_rtpbin (GstRTSPMedia * media, GstElement * rtpbin);

static void
aur_rtsp_media_factory_uri_class_init (AurRTSPMediaFactoryURIClass * aur_klass)
{
  GstRTSPMediaFactoryClass *mf_klass =
      (GstRTSPMediaFactoryClass *) (aur_klass);
  mf_klass->create_pipeline = create_pipeline;
}

static void
aur_rtsp_media_factory_uri_init (AurRTSPMediaFactoryURI * factory)
{
}

static GstElement *
create_pipeline (GstRTSPMediaFactory * factory, GstRTSPMedia * media)
{
  GstElement *pipeline;
  AurRTSPMediaFactoryURI *f = (AurRTSPMediaFactoryURI *)(factory);

  pipeline = gst_pipeline_new ("media-pipeline");
  gst_pipeline_use_clock (GST_PIPELINE (pipeline), f->netclock);
  gst_rtsp_media_take_pipeline (media, GST_PIPELINE_CAST (pipeline));

  return pipeline;
}

G_DEFINE_TYPE (AurRTSPMedia, aur_rtsp_media, GST_TYPE_RTSP_MEDIA);

static void
aur_rtsp_media_class_init (AurRTSPMediaClass * aur_klass)
{
  GstRTSPMediaClass *klass = (GstRTSPMediaClass *) (aur_klass);
  klass->setup_rtpbin = custom_setup_rtpbin;
}

static void
aur_rtsp_media_init (AurRTSPMedia * media)
{
}

static gboolean
custom_setup_rtpbin (GstRTSPMedia * media, GstElement * rtpbin)
{
  g_object_set (rtpbin, "ntp-time-source", 3, NULL);
  return TRUE;
}

