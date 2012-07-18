#ifndef __SNRA_CLIENT_H__
#define __SNRA_CLIENT_H__

#include <gst/gst.h>
#include <gst/net/gstnet.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include "snra-client-types.h"

G_BEGIN_DECLS

#define SNRA_TYPE_CLIENT (snra_client_get_type ())

typedef struct _SnraClientClass SnraClientClass;

struct _SnraClient
{
  GObject parent;

  GstClock *net_clock;
  gchar *server_host;

  SoupSession *soup;
  JsonParser *json;

  GstElement *player;

  guint timeout;
};

struct _SnraClientClass
{
  GObjectClass parent;
};

GType snra_client_get_type(void);
SnraClient *snra_client_new(const gchar *server);

G_END_DECLS
#endif
