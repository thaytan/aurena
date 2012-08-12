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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libsoup/soup-server.h>

#include "snra-server-client.h"

void
snra_server_client_send_message (SnraServerClient *client,
  gchar *body, gsize len)
{
  soup_message_body_append (client->event_pipe->response_body,
       SOUP_MEMORY_COPY, body, len);
  soup_server_unpause_message (client->soup, client->event_pipe);
}

void
snra_server_client_free (SnraServerClient *client)
{
  soup_message_body_complete (client->event_pipe->response_body);
  g_free (client);
}

