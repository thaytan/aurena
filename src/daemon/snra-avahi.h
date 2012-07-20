/* GStreamer
 * Copyright (C) 2012 Jan Schmidt <thaytan@noraisin.net>
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
#ifndef __SRNA_AVAHI_H__
#define __SRNA_AVAHI_H__

#include <glib.h>
#include <glib-object.h>

#include <src/snra-types.h>

G_BEGIN_DECLS

#define SNRA_TYPE_AVAHI (snra_avahi_get_type ())

typedef struct _SnraAvahiClass SnraAvahiClass;
typedef struct _SnraAvahiPrivate SnraAvahiPrivate;

struct _SnraAvahi
{
  GObject parent;
  SnraAvahiPrivate *priv;
};

struct _SnraAvahiClass
{
  GObjectClass parent;
};

GType snra_avahi_get_type(void);

SnraAvahi *snra_avahi_new(void);

G_END_DECLS

#endif
