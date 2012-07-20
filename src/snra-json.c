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

#include <gst/gst.h>
#include <json-glib/json-glib.h>

#include <src/snra-json.h>

static void
snra_gst_struct_from_object (G_GNUC_UNUSED JsonObject *o,
    const gchar *member_name, JsonNode *member_node,
    GstStructure *s)
{
  if (JSON_NODE_HOLDS_OBJECT (member_node)) {
    GstStructure *child = snra_json_to_gst_structure (member_node);
    gst_structure_set (s, member_name, GST_TYPE_STRUCTURE, child, NULL);
  }
  else {
    GValue v = G_VALUE_INIT;

    json_node_get_value (member_node, &v);
    gst_structure_set_value (s, member_name, &v);
    g_value_unset (&v);
  }
}

GstStructure *
snra_json_to_gst_structure (JsonNode *root)
{
  GstStructure *s = NULL;
  JsonObject *o;

  if (!JSON_NODE_HOLDS_OBJECT (root))
    return NULL;

  s = gst_structure_new ("json", NULL);

  o = json_node_get_object (root);
  json_object_foreach_member (o,
      (JsonObjectForeach) snra_gst_struct_from_object, s);

  return s;
}

static void
snra_add_struct_object (GQuark field_id, const GValue *value, JsonObject *o)
{
  JsonNode *n = NULL;

  if (GST_VALUE_HOLDS_STRUCTURE (value)) {
    const GstStructure *s = gst_value_get_structure (value);
    n = snra_json_from_gst_structure (s);
  }
  else {
    n = json_node_new (JSON_NODE_VALUE);
    json_node_set_value (n, value);
  }

  if (n)
    json_object_set_member (o, g_quark_to_string (field_id), n);
}

JsonNode *
snra_json_from_gst_structure (const GstStructure *s)
{
  JsonNode *root = json_node_new(JSON_NODE_OBJECT);

  json_node_take_object (root, json_object_new());

  gst_structure_foreach (s,
      (GstStructureForeachFunc) snra_add_struct_object,
      json_node_get_object (root));
  
  return root;
}

static gboolean
snra_json_structure_get_as (const GstStructure *structure,
    const gchar *fieldname, GType t, GValue *dest)
{
  const GValue *v1 = gst_structure_get_value (structure, fieldname);
  if (v1 == NULL)
    return FALSE;

  g_value_init (dest, t);
  g_value_transform (v1, dest);

  return TRUE;
}

gboolean 
snra_json_structure_get_int (const GstStructure *structure,
    const gchar *fieldname, gint *value)
{
  GValue dest = G_VALUE_INIT;
  gboolean res = snra_json_structure_get_as (structure, fieldname, G_TYPE_INT, &dest);

  if (res) {
    if (value)
      *value = g_value_get_int (&dest);
    g_value_unset (&dest);
  }
  return TRUE;
}

gboolean 
snra_json_structure_get_int64 (const GstStructure *structure,
    const gchar *fieldname, gint64 *value)
{
  GValue dest = G_VALUE_INIT;
  gboolean res = snra_json_structure_get_as (structure, fieldname, G_TYPE_INT64, &dest);

  if (res) {
    if (value)
      *value = g_value_get_int64 (&dest);
    g_value_unset (&dest);
  }
  return res;
}

gboolean 
snra_json_structure_get_double (const GstStructure *structure,
    const gchar *fieldname, gdouble *value)
{
  GValue dest = G_VALUE_INIT;
  gboolean res = snra_json_structure_get_as (structure, fieldname, G_TYPE_DOUBLE, &dest);

  if (res) {
    if (value)
      *value = g_value_get_double (&dest);
    g_value_unset (&dest);
  }
  return res;
}
