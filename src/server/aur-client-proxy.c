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

#include <common/aur-event.h>
#include <common/aur-component.h>
#include <server/aur-client-proxy.h>

G_DEFINE_TYPE (AurClientProxy, aur_client_proxy, G_TYPE_OBJECT);

static void aur_client_proxy_finalize (GObject * object);

static void
aur_client_proxy_init (AurClientProxy * proxy G_GNUC_UNUSED)
{
}

static void
aur_client_proxy_class_init (AurClientProxyClass * proxy_class)
{
  GObjectClass *object_class = (GObjectClass *) (proxy_class);

  object_class->finalize = aur_client_proxy_finalize;
}

static void
aur_client_proxy_finalize (GObject * object)
{
  AurClientProxy *proxy = (AurClientProxy *) (object);
  if (proxy->conn)
    g_object_unref (proxy->conn);

  g_free (proxy->record_path);
  g_free (proxy->host);
  g_free (proxy);
}
