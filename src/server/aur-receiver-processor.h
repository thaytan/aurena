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
#ifndef __AUR_RECEIVER_PROCESSOR_H__
#define __AUR_RECEIVER_PROCESSOR_H__

#include <gst/gst.h>
#include <server/aur-server-types.h>

G_BEGIN_DECLS

#define AUR_TYPE_RECEIVER_PROCESSOR (aur_receiver_processor_get_type ())

typedef struct _AurReceiverProcessorClass AurReceiverProcessorClass;
typedef struct _AurReceiverProcessorChannel AurReceiverProcessorChannel;
typedef struct _AurReceiverProcessorPrivate AurReceiverProcessorPrivate;

struct _AurReceiverProcessor
{
  GObject parent;
  AurReceiverProcessorPrivate *priv;

  AurReceiverProcessorChannel *channels[8];
  gint n_inuse;
  GstElement *pipeline;
  GstElement *filesink;
  GstState state;
};

struct _AurReceiverProcessorClass
{
  GObjectClass parent;
};

GType aur_receiver_processor_get_type(void);
AurReceiverProcessor *aur_receiver_processor_new();
AurReceiverProcessorChannel *aur_receiver_processor_get_channel (AurReceiverProcessor *processor);
void aur_receiver_processor_push_sample (AurReceiverProcessor *processor,
    AurReceiverProcessorChannel *channel, GstSample *sample, GstClockTime play_time);

void aur_receiver_processor_release_channel (AurReceiverProcessor *processor, AurReceiverProcessorChannel *channel);


G_END_DECLS
#endif
