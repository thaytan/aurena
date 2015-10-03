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
#ifndef __AUR_COMPONENT_LINK_H__
#define __AUR_COMPONENT_LINK_H__

#include <glib.h>
#include <glib-object.h>
#include <common/aur-types.h>
#include <common/aur-component.h>

G_BEGIN_DECLS

#define AUR_TYPE_COMPONENT_LINK (aur_component_link_get_type ())
#define AUR_COMPONENT_LINK(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), AUR_TYPE_COMPONENT_LINK, AurComponentLink))
#define AUR_COMPONENT_LINK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass),AUR_TYPE_COMPONENT_LINK, AurComponentLink))
#define AUR_IS_COMPONENT_LINK(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), AUR_TYPE_COMPONENT_LINK))

typedef struct _AurComponentLinkClass AurComponentLinkClass;

struct _AurComponentLink
{
  GObject parent;

  GList *components;
};

struct _AurComponentLinkClass
{
  GObjectClass parent;
};

GType aur_component_link_get_type(void);
AurComponentLink *aur_component_link_new ();
void aur_component_link_register_component (AurComponentLink *link, AurComponent *component);
gboolean aur_component_link_dispatch (AurComponentLink *link, AurComponentRole target_roles, AurEvent *event);

G_END_DECLS

#endif
