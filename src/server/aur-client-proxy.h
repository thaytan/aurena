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

#ifndef __AUR_CLIENT_PROXY_H__
#define __AUR_CLIENT_PROXY_H__

#include <gst/gst.h>

#include <src/server/aur-server-types.h>

G_BEGIN_DECLS

GType aur_client_proxy_get_type(void);

typedef struct _AurClientProxyClass AurClientProxyClass;

#define AUR_TYPE_CLIENT_PROXY (aur_client_proxy_get_type())
#define AUR_CLIENT_PROXY(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),AUR_TYPE_CLIENT_PROXY, AurClientProxy))
#define AUR_CLIENT_PROXY_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),AUR_TYPE_CLIENT_PROXY, AurClientProxy))
#define AUR_IS_CLIENT_PROXY(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),AUR_TYPE_CLIENT_PROXY))
#define AUR_IS_CLIENT_PROXY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),AUR_TYPE_CLIENT_PROXY))

struct _AurClientProxy
{
  GObject parent;

  AurComponentRole roles;
  AurHTTPClient *conn;

  guint id;
  gchar *host;

  gchar *record_path;

  gdouble volume;
  gboolean enabled;
  gboolean record_enabled;
};

struct _AurClientProxyClass
{
  GObjectClass parent;
};

G_END_DECLS
#endif

