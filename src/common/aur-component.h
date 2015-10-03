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
#ifndef __AUR_COMPONENT_H__
#define __AUR_COMPONENT_H__

#include <glib.h>
#include <glib-object.h>
#include <common/aur-types.h>

G_BEGIN_DECLS

#define AUR_TYPE_COMPONENT (aur_component_get_type ())
G_DECLARE_INTERFACE (AurComponent, aur_component, AUR, COMPONENT, GObject)

enum _AurComponentRole {
  AUR_COMPONENT_ROLE_MANAGER     = 0x1,
  AUR_COMPONENT_ROLE_CONTROLLER  = 0x2,
  AUR_COMPONENT_ROLE_PLAYER      = 0x4,
  AUR_COMPONENT_ROLE_CAPTURE     = 0x8
};

#define AUR_COMPONENT_ROLE_ALL (0xffff)

/* Interface type structure */
struct _AurComponentInterface
{
  GTypeInterface iface;

  AurComponentRole role;
  void (*receive_event) (AurComponent *component, AurEvent *event);
  void (*send_event) (AurComponent *component, AurComponentRole target_roles, AurEvent *event);
};

GType aur_component_get_type(void);
gboolean aur_component_has_role (AurComponent *component, AurComponentRole target_roles);
void aur_component_receive_event (AurComponent *component, AurEvent *event);
void aur_component_send_event (AurComponent *component, AurComponentRole target_roles, AurEvent *event);

G_END_DECLS

#endif
