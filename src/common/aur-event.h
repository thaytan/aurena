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
#ifndef __AUR_EVENT_H__
#define __AUR_EVENT_H__

#include <glib.h>
#include <glib-object.h>
#include <gst/gst.h>
#include <common/aur-types.h>
#include <common/aur-json.h>
#include <common/aur-component.h>

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE(AurEvent, aur_event, AUR, EVENT, GObject);

#define AUR_TYPE_EVENT (aur_event_get_type ())

struct _AurEvent
{
  GObject parent;

  GstStructure *fields;
};

struct _AurEventClass
{
  GObjectClass parent;
};

AurEvent *aur_event_new (GstStructure *fields);
const gchar *aur_event_get_name (const AurEvent *event);
JsonNode *aur_event_to_json_msg (const AurEvent *event, AurComponentRole targets);
gchar *aur_event_to_data (const AurEvent *event, AurComponentRole targets, gsize *len);

G_END_DECLS

#endif
