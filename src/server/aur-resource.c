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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "aur-resource.h"

const gchar *
aur_resource_get_mime_type (const gchar *filename)
{
  const gchar *extension;

  extension = g_strrstr (filename, ".");
  if (extension) {
    if (g_str_equal (extension, ".html"))
      return "text/html";
    if (g_str_equal (extension, ".png"))
      return "image/png";
    if (g_str_equal (extension, ".css"))
      return "text/css";
    if (g_str_equal (extension, ".jpg"))
      return "image/jpeg";
    if (g_str_equal (extension, ".js"))
      return "text/javascript";
  }

  return "text/plain";
}
