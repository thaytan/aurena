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

#ifndef __SNRA_SERVER_CLIENT_H__
#define __SNRA_SERVER_CLIENT_H__

#include <gst/gst.h>
#include <gst/net/gstnet.h>
#include <libsoup/soup.h>

#include <src/snra-types.h>

G_BEGIN_DECLS

#define SNRA_TYPE_SERVER_CLIENT (snra_server_client_get_type ())

typedef struct _SnraServerClientClass SnraServerClientClass;
typedef enum _SnraServerClientType SnraServerClientType;

enum _SnraServerClientType {
  SNRA_SERVER_CLIENT_CHUNKED,
  SNRA_SERVER_CLIENT_WEBSOCKET
};

struct _SnraServerClient
{
  GObject parent;

  SnraServerClientType type;
  gboolean fired_conn_lost;

  guint client_id;
  SoupMessage *event_pipe;
  SoupServer *soup;

  /* For talking to websocket clients */
  gint websocket_protocol;

  SoupSocket *socket;
  GIOChannel *io;
  guint io_watch;
  gchar *in_buf;
  gchar *in_bufptr;
  gsize in_bufsize;
  gsize in_bufavail;

  gchar *out_buf;
  gsize out_bufsize;

  GList *pending_msgs;
};

struct _SnraServerClientClass
{
  GObjectClass parent;
};

GType snra_server_client_get_type(void);

SnraServerClient *snra_server_client_new (SoupServer *soup,
    SoupMessage *msg, SoupClientContext *context);

void snra_server_client_send_message (SnraServerClient *client,
  gchar *body, gsize len);

G_END_DECLS

#endif
