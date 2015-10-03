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

#ifndef __AUR_HTTP_CLIENT_H__
#define __AUR_HTTP_CLIENT_H__

#include <gst/gst.h>
#include <gst/net/gstnet.h>
#include <libsoup/soup.h>

#include <src/server/aur-server-types.h>
#include <src/common/aur-websocket-parser.h>

G_BEGIN_DECLS

#define AUR_TYPE_HTTP_CLIENT (aur_http_client_get_type ())

typedef struct _AurHTTPClientClass AurHTTPClientClass;
typedef enum _AurHTTPClientType AurHTTPClientType;

enum _AurHTTPClientType {
  AUR_HTTP_CLIENT_CHUNKED,
  AUR_HTTP_CLIENT_WEBSOCKET,
  AUR_HTTP_CLIENT_SINGLE
};

struct _AurHTTPClient
{
  AurWebSocketParser parent;

  AurHTTPClientType type;
  gboolean fired_conn_lost;
  gboolean need_body_complete;

  guint conn_id;
  SoupMessage *event_pipe;
  SoupServer *soup;

  /* For talking to websocket clients */
  gint websocket_protocol;

  GSocket *socket;
  gchar *host;

  GIOChannel *io;
  guint io_watch;

  gchar *out_buf;
  gsize out_bufsize;

  GList *pending_msgs;

  gulong net_event_sig;
  gulong disco_sig;
  gulong wrote_info_sig;
};

struct _AurHTTPClientClass
{
  AurWebSocketParserClass parent;
};

GType aur_http_client_get_type(void);

AurHTTPClient *aur_http_client_new (SoupServer *soup,
    SoupMessage *msg, SoupClientContext *context);
AurHTTPClient *aur_http_client_new_single (SoupServer * soup,
    SoupMessage * msg, SoupClientContext * context);

void aur_http_client_send_message (AurHTTPClient *client,
  gchar *body, gsize len);

const gchar *aur_http_client_get_host (AurHTTPClient *client);

G_END_DECLS

#endif
