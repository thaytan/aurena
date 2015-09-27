/* GStreamer
 * Copyright (C) 2012-2014 Jan Schmidt <thaytan@noraisin.net>
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
#ifndef __AUR_TYPES_H__
#define __AUR_TYPES_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct _AurConfig AurConfig;

typedef enum _AurComponent AurComponent;

enum _AurComponent {
  AUR_COMPONENT_MANAGER     = 0x1,
  AUR_COMPONENT_CONTROLLER  = 0x2,
  AUR_COMPONENT_PLAYER      = 0x4,
  AUR_COMPONENT_CAPTURE     = 0x8
};

G_END_DECLS

#endif
