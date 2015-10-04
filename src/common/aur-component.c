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

#include "aur-component.h"
#include "aur-event.h"

enum
{
  SEND_EVENT,
  LAST_SIGNAL
};

G_DEFINE_INTERFACE (AurComponent, aur_component, G_TYPE_OBJECT);

static guint aur_component_signals[LAST_SIGNAL] = { 0 };

static gboolean
_aur_boolean_or_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return,
    gpointer dummy G_GNUC_UNUSED)
{
  gboolean myboolean;
  gboolean retboolean;

  myboolean = g_value_get_boolean (handler_return);
  retboolean = g_value_get_boolean (return_accu);

  if (!(ihint->run_type & G_SIGNAL_RUN_CLEANUP))
    g_value_set_boolean (return_accu, myboolean || retboolean);

  return TRUE;
}

static void
aur_component_default_init (AurComponentInterface * iface G_GNUC_UNUSED)
{
  aur_component_signals[SEND_EVENT] =
      g_signal_new ("send-event",
      AUR_TYPE_COMPONENT, G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (AurComponentInterface, send_event),
      _aur_boolean_or_accumulator, NULL, g_cclosure_marshal_generic,
      G_TYPE_BOOLEAN, 2, G_TYPE_INT, AUR_TYPE_EVENT);
}

gboolean
aur_component_has_role (AurComponent * component, AurComponentRole target_roles)
{
  AurComponentInterface *iface = AUR_COMPONENT_GET_IFACE (component);

  if (iface->role & target_roles)
    return TRUE;

  return FALSE;
}

void
aur_component_receive_event (AurComponent * component, AurEvent * event)
{
  g_return_if_fail (AUR_IS_COMPONENT (component));

  AUR_COMPONENT_GET_IFACE (component)->receive_event (component, event);
}

void
aur_component_send_event (AurComponent * component,
    AurComponentRole target_roles, AurEvent * event)
{
  gboolean handled = FALSE;

  g_return_if_fail (AUR_IS_COMPONENT (component));

  g_signal_emit (G_OBJECT (component),
      aur_component_signals[SEND_EVENT], 0, target_roles, event, &handled);
  if (!handled) {
    g_warning ("Event %s not handled", aur_event_get_name (event));
  }

  g_object_unref (G_OBJECT (event));
}

#define C_FLAGS(v) ((guint) v)

GType
aur_component_role_get_type (void)
{
  static const GFlagsValue values[] = {
    {C_FLAGS (AUR_COMPONENT_ROLE_MANAGER), "Server manager object", "manager"},
    {C_FLAGS (AUR_COMPONENT_ROLE_CONTROLLER), "Control client", "controller"},
    {C_FLAGS (AUR_COMPONENT_ROLE_PLAYER), "Playback client", "player"},
    {C_FLAGS (AUR_COMPONENT_ROLE_CAPTURE), "Capture client", "capture"},
    {0, NULL, NULL}
  };
  static volatile GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_flags_register_static ("AurComponentRole", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}
