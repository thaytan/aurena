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

#include "aur-receiver.h"
#include "aur-receiver-ingest.h"

static void aur_receiver_constructed (GObject * object);
static void aur_receiver_dispose (GObject * object);
static void aur_receiver_finalize (GObject * object);
static void aur_receiver_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void aur_receiver_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

enum
{
  PROP_0,
  PROP_RTSP_SERVER,
  PROP_LAST
};

G_DEFINE_TYPE (AurReceiver, aur_receiver, G_TYPE_OBJECT);

static void
aur_receiver_class_init (AurReceiverClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) (klass);

  object_class->constructed = aur_receiver_constructed;
  object_class->set_property = aur_receiver_set_property;
  object_class->get_property = aur_receiver_get_property;
  object_class->dispose = aur_receiver_dispose;
  object_class->finalize = aur_receiver_finalize;

  g_object_class_install_property (object_class, PROP_RTSP_SERVER,
      g_param_spec_object ("rtsp-server", "RTSP Server object",
          "RTSP server instance to use",
          GST_TYPE_RTSP_SERVER, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  
}

static void
aur_receiver_init (AurReceiver *receiver G_GNUC_UNUSED)
{
}

static void
aur_receiver_constructed (GObject * object)
{
  if (G_OBJECT_CLASS (aur_receiver_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (aur_receiver_parent_class)->constructed (object);
}

static void
aur_receiver_dispose (GObject * object)
{
  AurReceiver *receiver = (AurReceiver *) (object);

  if (receiver->rtsp) {
    gst_object_unref (receiver->rtsp);
    receiver->rtsp = NULL;
  }

  G_OBJECT_CLASS (aur_receiver_parent_class)->dispose (object);
}

static void
aur_receiver_finalize (GObject * object)
{
  //AurReceiver *receiver = (AurReceiver *) (object);

  G_OBJECT_CLASS (aur_receiver_parent_class)->finalize (object);
}

static void
aur_receiver_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  AurReceiver *receiver = (AurReceiver *) (object);

  switch (prop_id) {
    case PROP_RTSP_SERVER:
      receiver->rtsp = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
aur_receiver_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  AurReceiver *receiver = (AurReceiver *) (object);

  switch (prop_id) {
    case PROP_RTSP_SERVER:
      receiver->rtsp = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

AurReceiver *
aur_receiver_new(GstRTSPServer *rtsp)
{
  return g_object_new (AUR_TYPE_RECEIVER,
             "rtsp-server", rtsp, NULL);
}

gchar *
aur_receiver_get_record_dest (AurReceiver *receiver, guint client_id)
{
  GstRTSPServer *server;
  GstRTSPMountPoints *mounts;
  GstRTSPMediaFactory *factory;
  gchar *mount_point;

  g_return_val_if_fail (receiver->rtsp != NULL, NULL);

  server = receiver->rtsp;
  mounts = gst_rtsp_server_get_mount_points (server);

  factory = aur_receiver_ingest_factory_new ();
  gst_rtsp_media_factory_set_transport_mode (factory,
      GST_RTSP_TRANSPORT_MODE_RECORD);
  gst_rtsp_media_factory_set_launch (factory, "( decodebin name=depay0 ! queue ! audioconvert ! autoaudiosink )");
  gst_rtsp_media_factory_set_latency (factory, 40);

  mount_point = g_strdup_printf ("/record/%u", client_id);

  gst_rtsp_mount_points_add_factory (mounts, mount_point, factory);

  g_object_unref (mounts);

  return mount_point;
}
