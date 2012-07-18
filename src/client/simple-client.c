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

#include <glib.h>

#include <stdio.h>
#include <stdlib.h>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>

#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-glib/glib-watch.h>
#include <avahi-glib/glib-malloc.h>

#include "snra-client.h"

GMainLoop *ml = NULL;
gint sigint_received;

typedef struct _ClientContext ClientContext;
struct _ClientContext
{
  AvahiClient *avahi_client;
  SnraClient *client;
};

static void sigint_handler_sighandler (int signum);

static void
sigint_setup (void)
{
  struct sigaction action;
  memset (&action, 0, sizeof (struct sigaction));

  action.sa_handler = sigint_handler_sighandler;
  sigaction (SIGINT, &action, NULL);
}

static void
sigint_restore (void)
{
  struct sigaction action;
  memset (&action, 0, sizeof (struct sigaction));

  action.sa_handler = SIG_DFL;
  sigaction (SIGINT, &action, NULL);

}

static gboolean
sigint_check (G_GNUC_UNUSED void *data)
{
  if (sigint_received) {
    g_print ("Exiting...\n");
    g_main_loop_quit (ml);
  }
  return TRUE;
}

static void
sigint_handler_sighandler (G_GNUC_UNUSED int signum)
{
  sigint_received++;
  sigint_restore ();
}

static void
resolve_callback (AvahiServiceResolver * r,
    AVAHI_GCC_UNUSED AvahiIfIndex interface,
    AVAHI_GCC_UNUSED AvahiProtocol protocol,
    AvahiResolverEvent event, const char *name,
    const char *type, const char *domain, const char *host_name,
    const AvahiAddress * address, uint16_t port, AvahiStringList * txt,
    AvahiLookupResultFlags flags, AVAHI_GCC_UNUSED void *userdata)
{
  ClientContext *c = userdata;

  switch (event) {
    case AVAHI_RESOLVER_FAILURE:
      fprintf (stderr,
          "(Resolver) Failed to resolve service '%s' of type '%s' "
          "in domain '%s': %s\n", name, type, domain,
          avahi_strerror (avahi_client_errno (c->avahi_client)));
      break;

    case AVAHI_RESOLVER_FOUND:{
      char a[AVAHI_ADDRESS_STR_MAX], *t;

      fprintf (stderr, "Service '%s' of type '%s' in domain '%s':\n",
          name, type, domain);
      avahi_address_snprint (a, sizeof (a), address);

      t = avahi_string_list_to_string (txt);
      fprintf (stderr,
          "\t%s:%u (%s)\n" "\tTXT=%s\n"
          "\tcookie is %u\n" "\tis_local: %i\n"
          "\tour_own: %i\n" "\twide_area: %i\n" "\tmulticast: %i\n"
          "\tcached: %i\n",
          host_name, port, a, t,
          avahi_string_list_get_service_cookie (txt),
          ! !(flags & AVAHI_LOOKUP_RESULT_LOCAL),
          ! !(flags & AVAHI_LOOKUP_RESULT_OUR_OWN),
          ! !(flags & AVAHI_LOOKUP_RESULT_WIDE_AREA),
          ! !(flags & AVAHI_LOOKUP_RESULT_MULTICAST),
          ! !(flags & AVAHI_LOOKUP_RESULT_CACHED));
      avahi_free (t);
    }
  }

  avahi_service_resolver_free (r);
}

static void
browse_callback (AVAHI_GCC_UNUSED AvahiServiceBrowser *b,
    AvahiIfIndex interface, AvahiProtocol protocol,
    AvahiBrowserEvent event,
    const char *name, const char *type, const char *domain,
    AVAHI_GCC_UNUSED AvahiLookupResultFlags flags, void *userdata)
{

  ClientContext *c = userdata;

  switch (event) {
    case AVAHI_BROWSER_FAILURE:
      fprintf (stderr, "(Browser) %s\n",
          avahi_strerror (avahi_client_errno (c->avahi_client)));
      /* FIXME: Respawn browser on a timer? */
      g_main_loop_quit (ml);
      return;

    case AVAHI_BROWSER_NEW:{
      fprintf (stderr,
          "(Browser) NEW: service '%s' of type '%s' in domain '%s'\n",
          name, type, domain);
      if (!(avahi_service_resolver_new (c->avahi_client, interface,
                  protocol, name, type, domain, AVAHI_PROTO_UNSPEC, 0,
                  resolve_callback, c)))
        fprintf (stderr, "Failed to resolve service '%s': %s\n",
            name, avahi_strerror (avahi_client_errno (c->avahi_client)));
      break;
    }
    case AVAHI_BROWSER_REMOVE:
      fprintf (stderr,
          "(Browser) REMOVE: service '%s' of type '%s' in domain '%s'\n",
          name, type, domain);
      break;

    case AVAHI_BROWSER_ALL_FOR_NOW:
    case AVAHI_BROWSER_CACHE_EXHAUSTED:
      break;
  }
}

int
main (int argc, char *argv[])
{
  ClientContext ctx = { 0, };
  AvahiServiceBrowser *sb = NULL;
  AvahiGLibPoll *glib_poll = NULL;
  const AvahiPoll *poll_api;
  int error;
  int ret = 1;
  const gchar *server = NULL;

  gst_init (&argc, &argv);

  if (argc > 1) {
    /* Connect directly to the requested server, no avahi */
    server = argv[1];
  }

  avahi_set_allocator (avahi_glib_allocator ());

  g_timeout_add (250, sigint_check, NULL);
  sigint_setup ();

  ctx.client = snra_client_new (server);
  if (ctx.client == NULL)
    goto fail;

  ml = g_main_loop_new (NULL, FALSE);

  glib_poll = avahi_glib_poll_new (NULL, G_PRIORITY_DEFAULT);
  poll_api = avahi_glib_poll_get (glib_poll);

  ctx.avahi_client =
      avahi_client_new (poll_api, AVAHI_CLIENT_NO_FAIL,
          NULL, NULL, &error);
  if (ctx.avahi_client == NULL) {
    g_error ("Failed to create client: %s", avahi_strerror (error));
    goto fail;
  }

  sb = avahi_service_browser_new (ctx.avahi_client, AVAHI_IF_UNSPEC,
      AVAHI_PROTO_UNSPEC, "_sonarea._tcp", NULL, 0, browse_callback, &ctx);
  if (sb == NULL) {
    fprintf (stderr, "Failed to create service browser: %s\n",
        avahi_strerror (avahi_client_errno (ctx.avahi_client)));
    goto fail;
  }

  g_main_loop_run (ml);

  ret = 0;
fail:
  if (sb)
    avahi_service_browser_free (sb);
  if (ctx.avahi_client)
    avahi_client_free (ctx.avahi_client);
  if (ctx.client)
    g_object_unref (ctx.client);
  if (glib_poll)
    avahi_glib_poll_free (glib_poll);
  if (ml)
    g_main_loop_unref (ml);
  return ret;
}
