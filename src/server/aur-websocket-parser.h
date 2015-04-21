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

#ifndef __AUR_WEBSOCKET_PARSER_H__
#define __AUR_WEBSOCKET_PARSER_H__

#include <gst/gst.h>

#include <src/common/aur-types.h>

G_BEGIN_DECLS

#define AUR_TYPE_WEBSOCKET_PARSER (aur_websocket_parser_get_type ())
#define AUR_WEBSOCKET_PARSER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),AUR_TYPE_WEBSOCKET_PARSER, AurWebSocketParser))
#define AUR_WEBSOCKET_PARSER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),AUR_TYPE_WEBSOCKET_PARSER, AurWebSocketParser))
#define AUR_IS_WEBSOCKET_PARSER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),AUR_TYPE_WEBSOCKET_PARSER))
#define AUR_IS_WEBSOCKET_PARSER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),AUR_TYPE_WEBSOCKET_PARSER))
#define AUR_WEBSOCKET_PARSER_CAST(obj) ((AurWebSocketParser*)(obj))

GType aur_websocket_parser_get_type(void);

typedef struct _AurWebSocketParser AurWebSocketParser;
typedef struct _AurWebSocketParserClass AurWebSocketParserClass;
typedef enum _AurWebSocketParserResult AurWebSocketParserResult;

enum _AurWebSocketParserResult {
  AUR_WEBSOCKET_PARSER_OK,
  AUR_WEBSOCKET_PARSER_MORE_DATA,
  AUR_WEBSOCKET_PARSER_ERROR
};

struct _AurWebSocketParser
{
  GObject parent;

  gchar *in_buf;
  gchar *in_bufptr;
  gsize in_bufsize;
  gsize in_bufavail;
};

struct _AurWebSocketParserClass
{
  GObjectClass parent;

  void (*message_received) (AurWebSocketParser *parser, gchar *data, guint64 len);
};

AurWebSocketParser *aur_websocket_parser_new ();
GIOStatus aur_websocket_parser_read_io (AurWebSocketParser *p, GIOChannel *io);
#endif
