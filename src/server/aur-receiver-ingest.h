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
#ifndef __AUR_RECEIVER_INGEST_FACTORY_INGEST_H__
#define __AUR_RECEIVER_INGEST_FACTORY_INGEST_H__

#include <stdio.h>
#include <gst/gst.h>
#include <gst/net/gstnet.h>

#include <gst/rtsp-server/rtsp-server.h>
#include <server/aur-server-types.h>

G_BEGIN_DECLS

#define AUR_TYPE_RECEIVER_INGEST_FACTORY (aur_receiver_ingest_factory_get_type())
#define AUR_RECEIVER_INGEST_FACTORY(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),AUR_TYPE_RECEIVER_INGEST_FACTORY, AurReceiverIngestFactory))
#define AUR_RECEIVER_INGEST_FACTORY_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),AUR_TYPE_RECEIVER_INGEST_FACTORY, AurReceiverIngestFactory))
#define AUR_IS_RECEIVER_INGEST_FACTORY(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),AUR_TYPE_RECEIVER_INGEST_FACTORY))
#define AUR_IS_RECEIVER_INGEST_FACTORY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),AUR_TYPE_RECEIVER_INGEST_FACTORY))
#define AUR_RECEIVER_INGEST_FACTORY_CAST(obj) ((AurReceiverIngestFactory*)(obj))

#define AUR_TYPE_RECEIVER_INGEST_MEDIA   (aur_receiver_ingest_media_get_type ())
#define AUR_RECEIVER_INGEST_MEDIA(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),AUR_TYPE_RECEIVER_INGEST_MEDIA, AurReceiverIngestMedia))
#define AUR_RECEIVER_INGEST_MEDIA_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),AUR_TYPE_RECEIVER_INGEST_MEDIA, AurReceiverIngestMedia))
#define AUR_IS_RECEIVER_INGEST_MEDIA(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),AUR_TYPE_RECEIVER_INGEST_MEDIA))
#define AUR_IS_RECEIVER_INGEST_MEDIA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),AUR_TYPE_RECEIVER_INGEST_MEDIA))
#define AUR_RECEIVER_INGEST_MEDIA_CAST(obj) ((AurReceiverIngestMedia*)(obj))

typedef struct _AurReceiverIngestFactoryClass AurReceiverIngestFactoryClass;
typedef struct _AurReceiverIngestFactory AurReceiverIngestFactory;
typedef struct _AurReceiverIngestMediaClass AurReceiverIngestMediaClass;
typedef struct _AurReceiverIngestMedia AurReceiverIngestMedia;

struct _AurReceiverIngestFactory
{
  GstRTSPMediaFactory parent;
  AurReceiverProcessor *processor;
  guint id;
};

struct _AurReceiverIngestFactoryClass
{
  GstRTSPMediaFactoryClass parent;
};

struct _AurReceiverIngestMedia
{
  GstRTSPMedia parent;

  GstBin *bin;

  AurReceiverProcessor *processor;
  guint id;
};

struct _AurReceiverIngestMediaClass
{
  GstRTSPMediaClass parent;
};

GType aur_receiver_ingest_factory_get_type (void);
GType aur_receiver_ingest_media_get_type (void);

GstRTSPMediaFactory *aur_receiver_ingest_factory_new (guint id, AurReceiverProcessor *processor);

G_END_DECLS

#endif
