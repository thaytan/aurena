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

G_DEFINE_TYPE (SnraServerClient, snra_server_client, G_TYPE_OBJECT);

typedef struct _PendingMsg PendingMsg;

struct _PendingMsg
{
  gchar *body;
  gsize len;
};

static guint next_client_id = 1;

static void snra_server_client_finalize (GObject * object);
static void snra_server_client_dispose (GObject * object);

static void
snra_server_client_init (SnraServerClient * client)
{
  client->client_id = next_client_id++;
}

static void
snra_server_client_class_init (SnraServerClientClass * client_class)
{
  GObjectClass *gobject_class = (GObjectClass *) (client_class);

  gobject_class->dispose = snra_server_client_dispose;
  gobject_class->finalize = snra_server_client_finalize;
}

static void
snra_server_client_finalize (GObject * object)
{
  SnraServerClient *client = (SnraServerClient *) (object);

  if (client->type == SNRA_SERVER_CLIENT_CHUNKED) {
    soup_message_body_complete (client->event_pipe->response_body);
  } else {
    soup_socket_disconnect (client->socket);
  }

  g_free (client->in_buf);
  g_free (client->out_buf);

  G_OBJECT_CLASS (snra_server_client_parent_class)->finalize (object);
}

static void
snra_server_client_dispose (GObject * object)
{
  SnraServerClient *client = (SnraServerClient *) (object);

  if (client->io) {
    g_io_channel_shutdown (client->io, TRUE, NULL);
    g_io_channel_unref (client->io);
    client->io = NULL;
  }

  G_OBJECT_CLASS (snra_server_client_parent_class)->dispose (object);
}


/* Callbacks used for websocket clients */
static gboolean
try_parse_websocket_fragment (SnraServerClient * client)
{
  guint64 frag_size;
  gchar *header, *outptr;
  gchar *mask;
  gchar *decoded;
  gsize i, avail;

  g_print ("Got %u bytes to parse\n", (guint) client->in_bufavail);
  if (client->in_bufavail < 2)
    return FALSE;

  header = outptr = client->in_bufptr;
  avail = client->in_bufavail;

  if (header[0] & 0x80)
    g_print ("FIN flag. Payload type 0x%x\n", header[0] & 0xf);

  frag_size = header[1] & 0x7f;
  outptr += 2;

  if (frag_size < 126) {
    avail -= 2;
  } else if (frag_size == 126) {
    if (avail < 4)
      return FALSE;
    frag_size = GST_READ_UINT16_BE (outptr);
    outptr += 2;
    avail -= 8;
  } else {
    if (avail < 10)
      return FALSE;
    frag_size = GST_READ_UINT64_BE (outptr);
    outptr += 8;
    avail -= 8;
  }

  if ((header[1] & 0x80) == 0) {
    /* FIXME: drop the connection */
    g_print ("Received packet not masked. Skipping\n");
    goto skip_out;
  }

  if (avail < 4 + frag_size) {
    g_print ("Not enough bytes for mask & payload. Have %" G_GSIZE_FORMAT
        ", need %u bytes\n", avail, (guint) (4 + frag_size));
    return FALSE;
  }

  /* Consume the 4 mask bytes */
  mask = outptr;
  outptr += 4;
  avail -= 4;

  g_print ("Payload %u bytes mask 0x%02x%02x%02x%02x\n", (guint) (frag_size),
      (guint8) mask[0], (guint8) mask[1], (guint8) mask[2], (guint8) mask[3]);

  decoded = g_malloc (frag_size + 1);
  for (i = 0; i < frag_size; i++) {
    decoded[i] = outptr[i] ^ mask[i % 4];
  }
  decoded[frag_size] = 0;
  g_print ("Received: %s\n", decoded);

  /* FIXME: Fire a signal to get this packet processed */
  {
    gchar *testmsg = "{ string: \"A test reply message\"; }";
    snra_server_client_send_message (client, testmsg, strlen (testmsg));
  }

  g_free (decoded);

skip_out:
  {
    gsize consumed;

    outptr += frag_size;

    consumed = outptr - client->in_bufptr;

    client->in_bufavail -= consumed;
    client->in_bufptr = outptr;
  }

  return TRUE;
}

static gboolean
snra_server_client_io_cb (G_GNUC_UNUSED GIOChannel * source,
    GIOCondition condition, SnraServerClient * client)
{
  g_print ("Got IO callback for client %p w/ condition %u\n", client,
      (guint) (condition));

  if (condition & (G_IO_HUP | G_IO_ERR)) {
    soup_socket_disconnect (client->socket);
    return FALSE;
  }

  if (condition & G_IO_IN) {
    gsize bread;
    GIOStatus status;

    if (client->in_bufsize <= client->in_bufavail) {
      gsize cur_offs = client->in_bufptr - client->in_buf;

      client->in_bufsize *= 2;
      g_print ("Growing io_buf to %" G_GSIZE_FORMAT " bytes\n",
          client->in_bufsize);

      client->in_buf = g_renew (gchar, client->in_buf, client->in_bufsize);
      client->in_bufptr = client->in_buf + cur_offs;
    }

    status =
        g_io_channel_read_chars (client->io,
        client->in_buf + client->in_bufavail,
        client->in_bufsize - client->in_bufavail, &bread, NULL);

    if (status == G_IO_STATUS_EOF) {
      g_print ("Remote connection closed\n");
      soup_socket_disconnect (client->socket);
      return FALSE;
    }

    if (status == G_IO_STATUS_NORMAL) {
      g_print ("Collected %" G_GSIZE_FORMAT " bytes to io buf\n", bread);
      client->in_bufavail += bread;
    }

    while (client->in_bufavail > 0 && try_parse_websocket_fragment (client)) {
    };

    if (client->in_buf != client->in_bufptr) {
      memmove (client->in_buf, client->in_bufptr, client->in_bufavail);
      client->in_bufptr = client->in_buf;
    }
  }

  return TRUE;
}

static void
snra_server_client_wrote_headers (SoupMessage * msg, SnraServerClient * client)
{
  GList *cur;

  /* Pause the message so Soup doesn't do any more responding */
  soup_server_pause_message (client->soup, msg);

  g_print ("Adding IO watch for client %p\n", client);

  client->io = g_io_channel_unix_new (soup_socket_get_fd (client->socket));
  g_io_channel_set_encoding (client->io, NULL, NULL);
  g_io_channel_set_buffered (client->io, FALSE);
  g_io_add_watch (client->io, G_IO_IN | G_IO_HUP,
      (GIOFunc) (snra_server_client_io_cb), client);

  /* Send any pending messages */
  while ((cur = client->pending_msgs)) {
    GList *next;
    PendingMsg *msg = (PendingMsg *) (cur->data);

    next = g_list_next (cur);

    snra_server_client_send_message (client, msg->body, msg->len);
    client->pending_msgs = g_list_delete_link (client->pending_msgs, cur);
    g_free (msg->body);
    g_free (msg);

    cur = next;
  }
}

static gchar *
calc_websocket_challenge_reply (const gchar * key)
{
  const gchar *guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  gchar *ret = NULL;
  gchar *concat = g_strconcat (key, guid, NULL);
  GChecksum *checksum = g_checksum_new (G_CHECKSUM_SHA1);

  guint8 sha1[20];
  gsize len = 20;

  //g_print ("challenge: %s\n", key);

  g_checksum_update (checksum, (guchar *) (concat), -1);
  g_checksum_get_digest (checksum, sha1, &len);

  g_free (concat);

  ret = g_base64_encode (sha1, len);

  g_checksum_free (checksum);
  //g_print ("reply: %s\n", ret);

  return ret;
}

static gboolean
http_list_contains_value (const gchar * val, const gchar * needle)
{
  /* Connection params list must request upgrade to websocket */
  gchar **tmp = g_strsplit (val, ",", 0);
  gchar **cur;
  gboolean found_needle = FALSE;

  for (cur = tmp; *cur != NULL; cur++) {
    g_strstrip (*cur);
    if (g_ascii_strcasecmp (*cur, needle) == 0) {
      found_needle = TRUE;
      break;
    }
  }
  g_strfreev (tmp);
  return found_needle;
}

static gboolean
is_websocket_client (SnraServerClient * client)
{
  /* Check for request headers. Example:
   * Upgrade: websocket
   * Connection: Upgrade, Keep-Alive
   * Sec-WebSocket-Key: XYZABC123
   * Sec-WebSocket-Protocol: sonarea
   * Sec-WebSocket-Version: 13
   */
  SoupMessage *msg = client->event_pipe;
  SoupMessageHeaders *req_hdrs = msg->request_headers;
  const gchar *val;
  gint protocol_ver = 0;

  if ((val = soup_message_headers_get_one (req_hdrs, "Upgrade")) == NULL)
    return FALSE;
  if (g_ascii_strcasecmp (val, "websocket") != 0)
    return FALSE;
  if ((val = soup_message_headers_get_list (req_hdrs, "Connection")) == NULL)
    return FALSE;

  /* Connection params list must request upgrade to websocket */
  if (!http_list_contains_value (val, "upgrade"))
    return FALSE;

  if ((val =
          soup_message_headers_get_one (req_hdrs, "Sec-WebSocket-Key")) == NULL)
    return FALSE;
  if ((val =
          soup_message_headers_get_list (req_hdrs,
              "Sec-WebSocket-Protocol")) == NULL)
    return FALSE;

  if (!http_list_contains_value (val, "sonarea"))
    return FALSE;

  /* Requested protocol version must be 13 or 8 */
  if ((val = soup_message_headers_get_list (req_hdrs,
              "Sec-WebSocket-Version")) == NULL)
    return FALSE;

  if (http_list_contains_value (val, "13"))
    protocol_ver = 13;
  else if (http_list_contains_value (val, "8"))
    protocol_ver = 8;

  if (protocol_ver == 0)
    return FALSE;               /* No supported version found */

  g_print ("WebSocket connection with protocol %d\n", protocol_ver);
  client->websocket_protocol = protocol_ver;

  return TRUE;
}


SnraServerClient *
snra_server_client_new (SoupServer * soup, SoupMessage * msg,
    SoupClientContext * context)
{
  SnraServerClient *client = g_object_new (SNRA_TYPE_SERVER_CLIENT, NULL);
  const gchar *accept_challenge;
  gchar *accept_reply;

  client->soup = soup;
  client->event_pipe = msg;

  if (!is_websocket_client (client)) {
    client->type = SNRA_SERVER_CLIENT_CHUNKED;

    soup_message_headers_set_encoding (msg->response_headers,
        SOUP_ENCODING_CHUNKED);
    soup_message_set_status (msg, SOUP_STATUS_OK);
    return client;
  }

  /* Otherwise, it's a websocket client */
  client->type = SNRA_SERVER_CLIENT_WEBSOCKET;

  client->socket = soup_client_context_get_socket (context);
  client->in_bufptr = client->in_buf = g_new0 (gchar, 1024);
  client->in_bufsize = 1024;
  client->in_bufavail = 0;

  accept_challenge =
      soup_message_headers_get_one (msg->request_headers, "Sec-WebSocket-Key");
  accept_reply = calc_websocket_challenge_reply (accept_challenge);

  soup_message_headers_set_encoding (msg->response_headers, SOUP_ENCODING_EOF);

  soup_message_set_status (msg, SOUP_STATUS_SWITCHING_PROTOCOLS);
  soup_message_headers_replace (msg->response_headers, "Upgrade", "websocket");
  soup_message_headers_replace (msg->response_headers, "Connection", "Upgrade");
  soup_message_headers_replace (msg->response_headers, "Sec-WebSocket-Accept",
      accept_reply);
  soup_message_headers_replace (msg->response_headers, "Sec-WebSocket-Protocol",
      "sonarea");

  g_free (accept_reply);

  g_signal_connect (msg, "wrote-informational",
      G_CALLBACK (snra_server_client_wrote_headers), client);

  return client;
}

static void
write_to_io_channel (GIOChannel * io, const gchar * buf, gsize len)
{
  GIOStatus status;
  gsize tot_written = 0;

  do {
    gsize written = 0;
    status = g_io_channel_write_chars (io, buf, len, &written, NULL);
    tot_written += written;
    buf += written;
  } while (tot_written < len && status == G_IO_STATUS_AGAIN);
}

static void
write_fragment (SnraServerClient * client, gchar * body, gsize len)
{
  gchar header[14];
  gsize header_len, i;
  union
  {
    gchar bytes[4];
    guint32 val;
  } mask;

  header[0] = 0x81;
  header_len = 2;
  if (len < 126) {
    header[1] = 0x80 + len;
  } else if (len < 65536) {
    header[1] = 0x80 + 126;
    GST_WRITE_UINT16_BE (header + 2, (guint16) (len));
    header_len += 2;
  } else {
    header[1] = 0x80 + 127;
    GST_WRITE_UINT64_BE (header + 2, (guint64) (len));
    header_len += 8;
  }
  /* Add a random mask word. */
  mask.val = g_random_int ();

  memcpy (header + header_len, mask.bytes, 4);
  header_len += 4;

  /* Write WebSocket frame header */
  write_to_io_channel (client->io, header, header_len);

  /* Mask the body, and send it */
  if (len > client->out_bufsize) {
    client->out_bufsize = len;
    client->out_buf = g_realloc (client->out_buf, client->out_bufsize);
  }
  for (i = 0; i < len; i++) {
    client->out_buf[i] = body[i] ^ mask.bytes[i % 4];
  }

  write_to_io_channel (client->io, client->out_buf, len);
}

void
snra_server_client_send_message (SnraServerClient * client,
    gchar * body, gsize len)
{
  PendingMsg *msg;

  if (client->type == SNRA_SERVER_CLIENT_CHUNKED) {
    soup_message_body_append (client->event_pipe->response_body,
        SOUP_MEMORY_COPY, body, len);
    soup_server_unpause_message (client->soup, client->event_pipe);
    return;
  }

  if (client->io) {
    write_fragment (client, body, len);
    return;
  }

  /* Websocket isn't ready to send yet. Store as a pending message */
  msg = g_new (PendingMsg, 1);
  msg->len = len;
  msg->body = g_memdup (body, len);

  client->pending_msgs = g_list_append (client->pending_msgs, msg);
}
