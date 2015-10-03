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

#include <libsoup/soup-server.h>
#include <string.h>

#include "aur-websocket-parser.h"

enum
{
  MSG_RECEIVED,
  LAST_SIGNAL
};

G_DEFINE_TYPE (AurWebSocketParser, aur_websocket_parser, G_TYPE_OBJECT);

static guint aur_websocket_parser_signals[LAST_SIGNAL] = { 0 };

static void aur_websocket_parser_finalize (GObject * object);

AurWebSocketParser *
aur_websocket_parser_new ()
{
  return g_object_new (AUR_TYPE_WEBSOCKET_PARSER, NULL);
}

static void
aur_websocket_parser_init (AurWebSocketParser * parser)
{
  parser->in_bufsize = 1024;
  parser->in_bufptr = parser->in_buf = g_new0 (gchar, parser->in_bufsize);
  parser->in_bufavail = 0;
}

static void
aur_websocket_parser_class_init (AurWebSocketParserClass * parser_class)
{
  GObjectClass *gobject_class = (GObjectClass *) (parser_class);

  gobject_class->finalize = aur_websocket_parser_finalize;

  aur_websocket_parser_signals[MSG_RECEIVED] =
      g_signal_new ("message-received", G_TYPE_FROM_CLASS (parser_class),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (AurWebSocketParserClass,
          message_received), NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 2, G_TYPE_CHAR, G_TYPE_UINT64);
}

static void
aur_websocket_parser_finalize (GObject * object)
{
  AurWebSocketParser *parser = (AurWebSocketParser *) (object);

  g_free (parser->in_buf);

  G_OBJECT_CLASS (aur_websocket_parser_parent_class)->finalize (object);
}

static gboolean
try_parse_websocket_fragment (AurWebSocketParser * parser)
{
  guint64 frag_size;
  gchar *header, *outptr;
  gchar *mask;
  gchar *decoded;
  gsize i, avail;
  GIOStatus status = G_IO_STATUS_NORMAL;

  //g_print ("Got %u bytes to parse\n", (guint) parser->in_bufavail);
  if (parser->in_bufavail < 2)
    return G_IO_STATUS_AGAIN;

  header = outptr = parser->in_bufptr;
  avail = parser->in_bufavail;

#if 0
  if (header[0] & 0x80)
    g_print ("FIN flag. Payload type 0x%x\n", header[0] & 0xf);
#endif

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
    g_warning ("Received packet not masked. Dropping connection");
    status = G_IO_STATUS_ERROR;
    goto skip_out;
  }

  if (avail < 4 + frag_size) {
    /* Wait for more data */
    return G_IO_STATUS_AGAIN;
  }

  /* Consume the 4 mask bytes */
  mask = outptr;
  outptr += 4;
  avail -= 4;

  decoded = g_malloc (frag_size + 1);
  for (i = 0; i < frag_size; i++) {
    decoded[i] = outptr[i] ^ mask[i % 4];
  }
  decoded[frag_size] = 0;

  /* Fire a signal to get this packet processed */
#if 0
  g_print ("Have websocket msg of size %" G_GUINT64_FORMAT "\n", frag_size);
#endif
  g_signal_emit (parser, aur_websocket_parser_signals[MSG_RECEIVED], 0,
      decoded, (guint64) (frag_size));

  g_free (decoded);

skip_out:
  {
    gsize consumed;

    outptr += frag_size;

    consumed = outptr - parser->in_bufptr;

    parser->in_bufavail -= consumed;
    parser->in_bufptr = outptr;
  }

  return status;
}

GIOStatus
aur_websocket_parser_read_io (AurWebSocketParser * p, GIOChannel * io)
{
  GIOStatus status;
  gsize bread = 0;

  if (p->in_bufsize <= p->in_bufavail) {
    gsize cur_offs = p->in_bufptr - p->in_buf;

    p->in_bufsize *= 2;
    g_print ("Growing io_buf to %" G_GSIZE_FORMAT " bytes\n", p->in_bufsize);

    p->in_buf = g_renew (gchar, p->in_buf, p->in_bufsize);
    p->in_bufptr = p->in_buf + cur_offs;
  }

  do {
    status =
        g_io_channel_read_chars (io,
        p->in_buf + p->in_bufavail, p->in_bufsize - p->in_bufavail, &bread,
        NULL);
  } while (status == G_IO_STATUS_AGAIN);

  if (status != G_IO_STATUS_NORMAL)
    return status;

  p->in_bufavail += bread;

  while (p->in_bufavail > 0 && status == G_IO_STATUS_NORMAL)
    status = try_parse_websocket_fragment (p);

  if (status == G_IO_STATUS_AGAIN)
    status = G_IO_STATUS_NORMAL;

  if (p->in_buf != p->in_bufptr) {
    memmove (p->in_buf, p->in_bufptr, p->in_bufavail);
    p->in_bufptr = p->in_buf;
  }

  return status;
}
