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

#include <avahi-client/client.h>
#include <avahi-client/publish.h>

#include <avahi-common/error.h>
#include <avahi-common/alternative.h>

#include <avahi-glib/glib-watch.h>
#include <avahi-glib/glib-malloc.h>

#include "snra-avahi.h"

G_DEFINE_TYPE (SnraAvahi, snra_avahi, G_TYPE_OBJECT);

struct _SnraAvahiPrivate
{
  const AvahiPoll *poll_api;
  AvahiGLibPoll *glib_poll;
  AvahiClient *client;
  AvahiEntryGroup *group;
  char *service_name;
};

static void snra_avahi_finalize (GObject * object);

static void create_service (SnraAvahi * avahi);

static void
entry_group_callback (AvahiEntryGroup * g, AvahiEntryGroupState state,
    AVAHI_GCC_UNUSED void *userdata)
{
  SnraAvahi *avahi = (SnraAvahi *) (userdata);
  SnraAvahiPrivate *priv = avahi->priv;

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
      g_warning("Entry group failure: %s",
          avahi_strerror (avahi_client_errno (priv->client)));
      break;
    default:
      break;
  }
}

static void
create_service (SnraAvahi * avahi)
{
  SnraAvahiPrivate *priv = avahi->priv;
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
      g_message ("Adding service '%s'", priv->service_name);

      ret =
          avahi_entry_group_add_service (priv->group, AVAHI_IF_UNSPEC,
          AVAHI_PROTO_UNSPEC, 0, priv->service_name, "_sonarea._tcp", NULL,
          NULL, 5457, NULL);
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
          g_critical ("Failed to add _sonarea._tcp service: %s",
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
  SnraAvahi *avahi = (SnraAvahi *) (userdata);

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
  }
}

static void
snra_avahi_finalize (GObject * object)
{
  SnraAvahi *avahi = (SnraAvahi *) (object);

  avahi_entry_group_free (avahi->priv->group);
  avahi_client_free (avahi->priv->client);
  avahi_glib_poll_free (avahi->priv->glib_poll);
}

static void
snra_avahi_init (SnraAvahi * avahi)
{
  SnraAvahiPrivate *priv = avahi->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (avahi, SNRA_TYPE_AVAHI, SnraAvahiPrivate);
  int error = 0;

  priv->service_name = g_strdup ("Sonarea media server");

  priv->glib_poll = avahi_glib_poll_new (NULL, G_PRIORITY_DEFAULT);
  priv->poll_api = avahi_glib_poll_get (priv->glib_poll);

  priv->client =
      avahi_client_new (priv->poll_api, 0, avahi_client_callback, avahi,
      &error);

  /* Check the error return code */
  if (priv->client == NULL) {
    /* Print out the error string */
    g_warning ("Error initializing Avahi: %s", avahi_strerror (error));
  }
}

static void
snra_avahi_class_init (SnraAvahiClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) (klass);

  gobject_class->finalize = snra_avahi_finalize;

  g_type_class_add_private (gobject_class, sizeof (SnraAvahiPrivate));

  avahi_set_allocator (avahi_glib_allocator ());
}

SnraAvahi *
snra_avahi_new ()
{
  return g_object_new (SNRA_TYPE_AVAHI, NULL);
}
