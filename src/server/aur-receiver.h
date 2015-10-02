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
#ifndef __AUR_RECEIVER_H__
#define __AUR_RECEIVER_H__

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <src/server/aur-server-types.h>

G_BEGIN_DECLS

#define AUR_TYPE_RECEIVER (aur_receiver_get_type ())
#define AUR_RECEIVER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),AUR_TYPE_RECEIVER, AurReceiver))
#define AUR_RECEIVER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),AUR_TYPE_RECEIVER, AurReceiver))
#define AUR_IS_RECEIVER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),AUR_TYPE_RECEIVER))
#define AUR_IS_RECEIVER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),AUR_TYPE_RECEIVER))
#define AUR_RECEIVER_CAST(obj) ((AurReceiver*)(obj))

typedef struct _AurReceiverClass AurReceiverClass;

struct _AurReceiver
{
  GObject parent;

  GstRTSPServer *rtsp;
  AurReceiverProcessor *processor;
  /* Factories in the list owned by RTSP server */
  GList *ingests;
};

struct _AurReceiverClass
{
  GObjectClass parent;
};

GType aur_receiver_get_type(void);
AurReceiver *aur_receiver_new(GstRTSPServer *rtsp);

gchar *aur_receiver_get_record_dest (AurReceiver *receiver, guint client_id);

G_END_DECLS
#endif
