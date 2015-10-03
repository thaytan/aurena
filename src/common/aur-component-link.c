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

#include <gst/gst.h>

#include "aur-component-link.h"

GST_DEBUG_CATEGORY_STATIC (debug_cat);
#define GST_CAT_DEFAULT debug_cat

static void
_do_init ()
{
  GST_DEBUG_CATEGORY_INIT (debug_cat, "aurena/component-link", 0,
      "Aurena Component link debug");
}

G_DEFINE_TYPE_WITH_CODE (AurComponentLink, aur_component_link, G_TYPE_OBJECT,
    _do_init ());

static void aur_component_link_dispose (GObject * object);
static gboolean aur_component_link_event_handler (AurComponent * component,
    AurComponentRole target_roles, AurEvent * event, AurComponentLink * link);

static void
aur_component_link_class_init (AurComponentLinkClass * link_class)
{
  GObjectClass *gobject_class = (GObjectClass *) (link_class);

  gobject_class->dispose = aur_component_link_dispose;
}

static void
aur_component_link_init (AurComponentLink * link G_GNUC_UNUSED)
{
}

static void
aur_component_link_dispose (GObject * object)
{
  AurComponentLink *link = AUR_COMPONENT_LINK (object);
  GList *cur;

  for (cur = link->components; cur != NULL; cur = g_list_next (cur)) {
    GObject *o = G_OBJECT (cur->data);

    g_signal_handlers_disconnect_by_func (o, aur_component_link_event_handler,
        link);
    g_object_unref (o);
  }

  g_list_free (link->components);
  link->components = NULL;
}

AurComponentLink *
aur_component_link_new ()
{
  AurComponentLink *link = g_object_new (AUR_TYPE_COMPONENT_LINK, NULL);

  return link;
}

static gboolean
aur_component_link_event_handler (AurComponent * component G_GNUC_UNUSED,
    AurComponentRole target_roles, AurEvent * event, AurComponentLink * link)
{
  return aur_component_link_dispatch (link, target_roles, event);
}

/* Takes ownership of the component passed */
void
aur_component_link_register_component (AurComponentLink * link,
    AurComponent * component)
{
  g_return_if_fail (AUR_IS_COMPONENT_LINK (link));
  g_return_if_fail (AUR_IS_COMPONENT (component));

  link->components = g_list_prepend (link->components, component);
  g_signal_connect (component, "send-event",
      G_CALLBACK (aur_component_link_event_handler), link);
}

gboolean
aur_component_link_dispatch (AurComponentLink * link,
    AurComponentRole target_roles, AurEvent * event)
{
  GList *cur;
  gboolean ret = FALSE;

  for (cur = link->components; cur != NULL; cur = g_list_next (cur)) {
    AurComponent *c = AUR_COMPONENT (cur->data);
    if (aur_component_has_role (c, target_roles)) {
      aur_component_receive_event (c, event);
      ret |= TRUE;
    }
  }

  return ret;
}
