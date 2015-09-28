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

#include "aur-receiver-ingest.h"

static GstElement *create_pipeline (GstRTSPMediaFactory * factory,
    GstRTSPMedia * media);

G_DEFINE_TYPE (AurReceiverIngestFactory, aur_receiver_ingest_factory,
    GST_TYPE_RTSP_MEDIA_FACTORY);

static gboolean custom_setup_rtpbin (GstRTSPMedia * media, GstElement * rtpbin);

static void
aur_receiver_ingest_factory_class_init (AurReceiverIngestFactoryClass * factory_klass)
{
  GstRTSPMediaFactoryClass *mf_klass =
      (GstRTSPMediaFactoryClass *) (factory_klass);
  mf_klass->create_pipeline = create_pipeline;
}

static void
aur_receiver_ingest_factory_init (AurReceiverIngestFactory * factory G_GNUC_UNUSED)
{
}

static GstElement *
create_pipeline (GstRTSPMediaFactory * factory G_GNUC_UNUSED, GstRTSPMedia * media)
{
  GstElement *pipeline;

  pipeline = gst_pipeline_new ("media-pipeline");
  gst_pipeline_use_clock (GST_PIPELINE (pipeline), gst_system_clock_obtain ());
  gst_rtsp_media_take_pipeline (media, GST_PIPELINE_CAST (pipeline));

  return pipeline;
}

G_DEFINE_TYPE (AurReceiverIngestMedia, aur_receiver_ingest_media,
    GST_TYPE_RTSP_MEDIA);

static void
aur_receiver_ingest_media_class_init (AurReceiverIngestMediaClass * media_klass)
{
  GstRTSPMediaClass *klass = (GstRTSPMediaClass *) (media_klass);
  klass->setup_rtpbin = custom_setup_rtpbin;
}

static void
aur_receiver_ingest_media_init (AurReceiverIngestMedia * media G_GNUC_UNUSED)
{
}

static gboolean
custom_setup_rtpbin (GstRTSPMedia * media G_GNUC_UNUSED, GstElement * rtpbin)
{
  g_object_set (rtpbin, "ntp-time-source", 3, NULL);
  return TRUE;
}

GstRTSPMediaFactory *
aur_receiver_ingest_factory_new (void)
{
  GstRTSPMediaFactory *result;

  result = g_object_new (AUR_TYPE_RECEIVER_INGEST_FACTORY, NULL);

  return result;
}
