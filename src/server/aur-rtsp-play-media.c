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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "aur-rtsp-play-media.h"

#define AUR_TYPE_RTSP_PLAY_MEDIA_FACTORY      (aur_rtsp_play_media_factory_get_type ())
#define AUR_TYPE_RTSP_PLAY_MEDIA              (aur_rtsp_play_media_get_type ())

GType aur_rtsp_play_media_factory_get_type (void);
GType aur_rtsp_play_media_get_type (void);

static GstElement *create_pipeline (GstRTSPMediaFactory * factory,
    GstRTSPMedia * media);

struct _AurRTSPPlayMediaFactoryClass
{
  GstRTSPMediaFactoryURIClass parent;
};

struct _AurRTSPPlayMediaFactory
{
  GstRTSPMediaFactoryURI parent;
};

typedef struct _AurRTSPPlayMediaClass AurRTSPPlayMediaClass;
typedef struct _AurRTSPPlayMedia AurRTSPPlayMedia;

struct _AurRTSPPlayMediaClass
{
  GstRTSPMediaClass parent;
};

struct _AurRTSPPlayMedia
{
  GstRTSPMedia parent;
};

AurRTSPPlayMediaFactory *
aur_rtsp_play_media_factory_new ()
{
  AurRTSPPlayMediaFactory *result;

  result = g_object_new (AUR_TYPE_RTSP_PLAY_MEDIA_FACTORY, NULL);

  return result;
}

G_DEFINE_TYPE (AurRTSPPlayMediaFactory, aur_rtsp_play_media_factory,
    GST_TYPE_RTSP_MEDIA_FACTORY_URI);

static gboolean custom_setup_rtpbin (GstRTSPMedia * media, GstElement * rtpbin);

static void
aur_rtsp_play_media_factory_class_init (AurRTSPPlayMediaFactoryClass * klass)
{
  GstRTSPMediaFactoryClass *mf_klass = (GstRTSPMediaFactoryClass *) (klass);
  mf_klass->create_pipeline = create_pipeline;
}

static void
aur_rtsp_play_media_factory_init (AurRTSPPlayMediaFactory *
    factory G_GNUC_UNUSED)
{
}

static GstElement *
create_pipeline (GstRTSPMediaFactory * factory G_GNUC_UNUSED,
    GstRTSPMedia * media)
{
  GstElement *pipeline;

  pipeline = gst_pipeline_new ("media-pipeline");
  gst_pipeline_use_clock (GST_PIPELINE (pipeline), gst_system_clock_obtain ());
  gst_rtsp_media_take_pipeline (media, GST_PIPELINE_CAST (pipeline));

  return pipeline;
}

G_DEFINE_TYPE (AurRTSPPlayMedia, aur_rtsp_play_media, GST_TYPE_RTSP_MEDIA);

static void
aur_rtsp_play_media_class_init (AurRTSPPlayMediaClass * klass)
{
  GstRTSPMediaClass *media_klass = (GstRTSPMediaClass *) (klass);
  media_klass->setup_rtpbin = custom_setup_rtpbin;
}

static void
aur_rtsp_play_media_init (AurRTSPPlayMedia * media G_GNUC_UNUSED)
{
}

static gboolean
custom_setup_rtpbin (GstRTSPMedia * media G_GNUC_UNUSED, GstElement * rtpbin)
{
  g_object_set (rtpbin, "ntp-time-source", 3, NULL);
  return TRUE;
}
