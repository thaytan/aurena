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
 * Aurena Receiver manages ingest objects to receive
 * streams and the processor object that mixes them and
 * outputs the 8-channel stream for ManyEars
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "aur-receiver.h"
#include "aur-receiver-ingest.h"
#include "aur-receiver-processor.h"

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
aur_receiver_init (AurReceiver * receiver)
{
  receiver->processor = aur_receiver_processor_new ();
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
  AurReceiver *receiver = AUR_RECEIVER (object);

  if (receiver->rtsp) {
    gst_object_unref (receiver->rtsp);
    receiver->rtsp = NULL;
  }

  if (receiver->processor) {
    g_object_unref (G_OBJECT (receiver->processor));
    receiver->processor = NULL;
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
aur_receiver_new (GstRTSPServer * rtsp)
{
  return g_object_new (AUR_TYPE_RECEIVER, "rtsp-server", rtsp, NULL);
}

static GstRTSPMediaFactory *
lookup_by_client_id (AurReceiver * receiver, guint client_id)
{
  GList *cur;
  for (cur = receiver->ingests; cur != NULL; cur = g_list_next (cur)) {
    AurReceiverIngestFactory *factory =
        (AurReceiverIngestFactory *) (cur->data);
    if (factory->id == client_id)
      return (GstRTSPMediaFactory *) factory;
  }
  return NULL;
}

gchar *
aur_receiver_get_record_dest (AurReceiver * receiver, guint client_id)
{
  GstRTSPMediaFactory *factory;
  gchar *mount_point;

  g_return_val_if_fail (receiver->rtsp != NULL, NULL);

  mount_point = g_strdup_printf ("/record/%u", client_id);

  factory = lookup_by_client_id (receiver, client_id);
  if (factory == NULL) {
    GstRTSPServer *server;
    GstRTSPMountPoints *mounts;

    factory = aur_receiver_ingest_factory_new (client_id, receiver->processor);

    receiver->ingests = g_list_prepend (receiver->ingests, factory);

    server = receiver->rtsp;
    mounts = gst_rtsp_server_get_mount_points (server);
    gst_rtsp_mount_points_add_factory (mounts, mount_point, factory);
    g_object_unref (mounts);
  }

  return mount_point;
}
