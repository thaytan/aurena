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

#include <avahi-client/client.h>
#include <avahi-client/publish.h>

#include <avahi-common/error.h>
#include <avahi-common/alternative.h>

#include <avahi-glib/glib-watch.h>
#include <avahi-glib/glib-malloc.h>

#include "aur-avahi.h"

enum
{
  PROP_0,
  PROP_PORT,
  PROP_LAST
};

static void aur_avahi_constructed (GObject * object);
static void aur_avahi_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void aur_avahi_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

struct _AurAvahiPrivate
{
  const AvahiPoll *poll_api;
  AvahiGLibPoll *glib_poll;
  AvahiClient *client;
  AvahiEntryGroup *group;
  char *service_name;
  int port;
};

G_DEFINE_TYPE_WITH_PRIVATE  (AurAvahi, aur_avahi, G_TYPE_OBJECT);

static void aur_avahi_finalize (GObject * object);

static void create_service (AurAvahi * avahi);

static void
entry_group_callback (AVAHI_GCC_UNUSED AvahiEntryGroup * g,
    AvahiEntryGroupState state, AVAHI_GCC_UNUSED void *userdata)
{
  AurAvahi *avahi = (AurAvahi *) (userdata);
  AurAvahiPrivate *priv = avahi->priv;

  switch (state) {
    case AVAHI_ENTRY_GROUP_ESTABLISHED:
      /* The entry group has been established successfully */
      g_print ("Service '%s' successfully established.\n", priv->service_name);
      break;
    case AVAHI_ENTRY_GROUP_COLLISION:{
      char *n;

      /* A service name collision with a remote service
       * happened. Let's pick a new name */
      n = avahi_alternative_service_name (priv->service_name);
      avahi_free (priv->service_name);
      priv->service_name = n;

      g_message ("Service name collision, renaming service to '%s'",
          priv->service_name);

      /* And recreate the services */
      create_service (avahi);
      break;
    }
    case AVAHI_ENTRY_GROUP_FAILURE:
      g_warning ("Entry group failure: %s",
          avahi_strerror (avahi_client_errno (priv->client)));
      break;
    default:
      break;
  }
}

static void
create_service (AurAvahi * avahi)
{
  AurAvahiPrivate *priv = avahi->priv;
  int ret;

  do {
    if (priv->group == NULL) {
      priv->group =
          avahi_entry_group_new (priv->client, entry_group_callback, avahi);
      if (priv->group == NULL) {
        g_critical ("avahi_entry_group_new() failed: %s\n",
            avahi_strerror (avahi_client_errno (priv->client)));
        goto fail;
      }
    }

    /* If the group is empty (either because it was just created, or
     * because it was reset previously, add our entries.  */
    if (avahi_entry_group_is_empty (priv->group)) {
      g_message ("Adding service '%s' on port %d", priv->service_name,
          priv->port);

      ret =
          avahi_entry_group_add_service (priv->group, AVAHI_IF_UNSPEC,
          AVAHI_PROTO_UNSPEC, 0, priv->service_name, "_aurena._tcp", NULL,
          NULL, priv->port, NULL);
      if (ret < 0) {
        if (ret == AVAHI_ERR_COLLISION) {
          /* A service name collision with a local service happened. Let's
           * pick a new name */
          char *n = avahi_alternative_service_name (priv->service_name);
          g_free (priv->service_name);
          priv->service_name = n;
          g_message ("Service name collision, renaming service to '%s'\n",
              priv->service_name);
          avahi_entry_group_reset (priv->group);
          continue;
        } else {
          g_critical ("Failed to add _aurena._tcp service: %s",
              avahi_strerror (ret));
          goto fail;
        }
      }
#if 0
      /* Add an additional (hypothetic) subtype */
      if ((ret =
              avahi_entry_group_add_service_subtype (group, AVAHI_IF_UNSPEC,
                  AVAHI_PROTO_UNSPEC, 0, name, "_printer._tcp", NULL,
                  "_magic._sub._printer._tcp") < 0)) {
        fprintf (stderr,
            "Failed to add subtype _magic._sub._printer._tcp: %s\n",
            avahi_strerror (ret));
        goto fail;
      }
#endif

      if ((ret = avahi_entry_group_commit (priv->group)) < 0) {
        g_critical ("Failed to commit entry group: %s\n", avahi_strerror (ret));
      }
    }
    return;
  } while (TRUE);

fail:
  return;
}

static void
avahi_client_callback (AVAHI_GCC_UNUSED AvahiClient * client,
    AvahiClientState state, void *userdata)
{
  AurAvahi *avahi = (AurAvahi *) (userdata);

  switch (state) {
    case AVAHI_CLIENT_S_RUNNING:
      /* Create services now */
      avahi->priv->client = client;
      create_service (avahi);
      break;
    case AVAHI_CLIENT_FAILURE:
      g_error ("Client failure: %s\n",
          avahi_strerror (avahi_client_errno (client)));
      break;
    case AVAHI_CLIENT_S_COLLISION:
      /* Let's drop our registered services. When the server is back
       * in AVAHI_SERVER_RUNNING state we will register them
       * again with the new host name. */
    case AVAHI_CLIENT_S_REGISTERING:
      /* The server records are now being established. This
       * might be caused by a host name change. We need to wait
       * to records to register until the host name is
       * properly established - so clear out any pending
       * registration group. */
      if (avahi->priv->group)
        avahi_entry_group_reset (avahi->priv->group);
      break;
    default:
      break;
  }
}

static void
aur_avahi_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  AurAvahi *avahi = (AurAvahi *) (object);

  switch (prop_id) {
    case PROP_PORT:
      avahi->priv->port = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
aur_avahi_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  AurAvahi *avahi = (AurAvahi *) (object);

  switch (prop_id) {
    case PROP_PORT:
      g_value_set_int (value, avahi->priv->port);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
aur_avahi_finalize (GObject * object)
{
  AurAvahi *avahi = (AurAvahi *) (object);

  if (avahi->priv->group)
    avahi_entry_group_free (avahi->priv->group);
  if (avahi->priv->client)
    avahi_client_free (avahi->priv->client);

  avahi_glib_poll_free (avahi->priv->glib_poll);
}

static void
aur_avahi_init (AurAvahi * avahi)
{
  AurAvahiPrivate *priv = avahi->priv = aur_avahi_get_instance_private (avahi);

  priv->service_name = g_strdup ("Aurena media server");

  priv->glib_poll = avahi_glib_poll_new (NULL, G_PRIORITY_DEFAULT);
  priv->poll_api = avahi_glib_poll_get (priv->glib_poll);
  priv->port = 5457;
}

static void
aur_avahi_class_init (AurAvahiClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) (klass);

  gobject_class->constructed = aur_avahi_constructed;
  gobject_class->finalize = aur_avahi_finalize;
  gobject_class->set_property = aur_avahi_set_property;
  gobject_class->get_property = aur_avahi_get_property;

  g_object_class_install_property (gobject_class, PROP_PORT,
      g_param_spec_int ("aur-port", "Aurena port",
          "port for Aurena service",
          1, 65535, 5457, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  avahi_set_allocator (avahi_glib_allocator ());
}

static void
aur_avahi_constructed (GObject * object)
{
  AurAvahi *avahi = (AurAvahi *) (object);
  AurAvahiPrivate *priv = avahi->priv;
  int error = 0;

  if (G_OBJECT_CLASS (aur_avahi_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (aur_avahi_parent_class)->constructed (object);

  priv->client =
      avahi_client_new (priv->poll_api, 0, avahi_client_callback, avahi,
      &error);

  /* Check the error return code */
  if (priv->client == NULL) {
    /* Print out the error string */
    g_warning ("Error initializing Avahi: %s", avahi_strerror (error));
  }
}

AurAvahi *
aur_avahi_new (int port)
{
  return g_object_new (AUR_TYPE_AVAHI, "aur-port", port, NULL);
}
