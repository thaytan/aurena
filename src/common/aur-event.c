/* GStreamer
 * Copyright (C) 2012-2015 Jan Schmidt <jan@centricular.com>
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

#include "aur-event.h"
#include "aur-component.h"

GST_DEBUG_CATEGORY_STATIC (debug_cat);
#define GST_CAT_DEFAULT debug_cat

static void
_do_init ()
{
  GST_DEBUG_CATEGORY_INIT (debug_cat, "aurena/event", 0, "Aurena event debug");
}

G_DEFINE_TYPE_WITH_CODE (AurEvent, aur_event, G_TYPE_OBJECT, _do_init ());

static void aur_event_finalize (GObject * object);

static void
aur_event_class_init (AurEventClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) (klass);

  gobject_class->finalize = aur_event_finalize;
}

static void
aur_event_init (AurEvent * event G_GNUC_UNUSED)
{
}

static void
aur_event_finalize (GObject * object)
{
  AurEvent *event = AUR_EVENT (object);

  if (event->fields)
    gst_structure_free (event->fields);
}

const gchar *
aur_event_get_name (const AurEvent * event)
{
  g_return_val_if_fail (AUR_IS_EVENT (event), NULL);
  g_return_val_if_fail (event->fields, NULL);

  return gst_structure_get_string (event->fields, "msg-type");
}

AurEvent *
aur_event_new (GstStructure * fields)
{
  AurEvent *event;

  g_return_val_if_fail (fields != NULL, FALSE);

  event = g_object_new (AUR_TYPE_EVENT, NULL);
  event->fields = fields;

  return event;
}

JsonNode *
aur_event_to_json_msg (const AurEvent * event, AurComponentRole targets)
{
  GstStructure *msg;
  JsonNode *fields;

  g_return_val_if_fail (AUR_IS_EVENT (event), NULL);
  g_return_val_if_fail (event->fields, NULL);

  msg = gst_structure_copy (event->fields);

  gst_structure_set (msg,
            "msg-type", G_TYPE_STRING, aur_event_get_name (event),
            "msg-targets", G_TYPE_UINT, (guint) targets,
            NULL);

  fields = aur_json_from_gst_structure (msg);

  gst_structure_free (msg);
  return fields;
}
