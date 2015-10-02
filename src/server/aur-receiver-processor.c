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
 * Aurena Receiver Processor receives incoming audio feeds
 * from Aurena Receiver Ingest objects, mixes them and
 * outputs the 8-channel stream for ManyEars
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "aur-receiver-processor.h"

static void aur_receiver_processor_constructed (GObject * object);
static void aur_receiver_processor_dispose (GObject * object);
static void aur_receiver_processor_finalize (GObject * object);
static void aur_receiver_processor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void aur_receiver_processor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

enum
{
  PROP_0,
  PROP_LAST
};

G_DEFINE_TYPE (AurReceiverProcessor, aur_receiver_processor, G_TYPE_OBJECT);

static void
aur_receiver_processor_class_init (AurReceiverProcessorClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) (klass);

  object_class->constructed = aur_receiver_processor_constructed;
  object_class->set_property = aur_receiver_processor_set_property;
  object_class->get_property = aur_receiver_processor_get_property;
  object_class->dispose = aur_receiver_processor_dispose;
  object_class->finalize = aur_receiver_processor_finalize;

}

static void
aur_receiver_processor_init (AurReceiverProcessor *receiver G_GNUC_UNUSED)
{
}

static void
aur_receiver_processor_constructed (GObject * object)
{
  if (G_OBJECT_CLASS (aur_receiver_processor_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (aur_receiver_processor_parent_class)->constructed (object);
}

static void
aur_receiver_processor_dispose (GObject * object)
{
  AurReceiverProcessor *receiver = (AurReceiverProcessor *) (object);

  G_OBJECT_CLASS (aur_receiver_processor_parent_class)->dispose (object);
}

static void
aur_receiver_processor_finalize (GObject * object)
{
  //AurReceiverProcessor *receiver = (AurReceiverProcessor *) (object);

  G_OBJECT_CLASS (aur_receiver_processor_parent_class)->finalize (object);
}

static void
aur_receiver_processor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  AurReceiverProcessor *receiver = (AurReceiverProcessor *) (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
aur_receiver_processor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  AurReceiverProcessor *receiver = (AurReceiverProcessor *) (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

AurReceiverProcessor *
aur_receiver_processor_new()
{
  return g_object_new (AUR_TYPE_RECEIVER_PROCESSOR, NULL);
}
