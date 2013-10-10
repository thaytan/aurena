#ifdef CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <gio/gio.h>
#include <gio/gio.h>
#include <gst/net/gstnettimepacket.h>

#ifdef G_OS_UNIX
#include <glib-unix.h>
#include <sys/wait.h>
#endif

typedef struct PortInfo
{
  GSocket *socket; /* socket we used to send packet to the master */
  GSocketAddress *src_address; /* Where to return the packet */
  GInetAddress *inet_addr; /* The client's inet_addr, from src_address */
  gint port; /* The client's port, from src_address */
} PortInfo;

GMainLoop *loop;
GSocket *masterSocket;
GSocketAddress *serverAddress;
GList *ports = NULL;

static void listen_on_socket(gint listenPort, GSourceFunc handler, gpointer user_data);
static void send_packet_to_master(GSocketAddress *src_address, gchar *buf);

static gboolean
intr_handler (G_GNUC_UNUSED gpointer user_data)
{
  g_print("Exiting.\n");
  g_main_loop_quit(loop);
  return FALSE;
}

static gboolean
receive_clock_packet(GSocket *socket, G_GNUC_UNUSED GIOCondition condition,
    gpointer user_data)
{
  gchar buffer[GST_NET_TIME_PACKET_SIZE];
  GSocketAddress *src_address;
  gssize ret;

  ret = g_socket_receive_from (socket, &src_address, buffer,
            GST_NET_TIME_PACKET_SIZE, NULL, NULL);
  if (ret < GST_NET_TIME_PACKET_SIZE) {
    g_print ("Packet too small: %" G_GSSIZE_FORMAT "\n", ret);
    return TRUE;
  }

  if (user_data == NULL) {
    send_packet_to_master(src_address, buffer);
  }
  else {
    /* Return the reply to the client */
    PortInfo *portinfo = (PortInfo *)(user_data);
    g_socket_send_to (masterSocket, portinfo->src_address, (const gchar *) buffer,
      GST_NET_TIME_PACKET_SIZE, NULL, NULL);
  }
  return TRUE;
}


static void
send_packet_to_master(GSocketAddress *src_address, gchar *buf)
{
  PortInfo *portinfo = NULL; 
  GList *cur;
  GInetAddress *inet_addr;
  gint inet_port;

  /* Locate the socket for this source address */
  inet_port = g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (src_address));
  inet_addr =
      g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (src_address));
  cur = g_list_first(ports);
  while (cur != NULL) {
    PortInfo *cur_port = (PortInfo *)(cur->data);
    if (g_inet_address_equal (inet_addr, cur_port->inet_addr) && inet_port == cur_port->port) {
      portinfo = cur_port;
      break;
    }
    cur = g_list_next(cur);
  }

  /* Otherwise, create one */
  if (portinfo == NULL) {
    gchar *tmp;

    portinfo = g_new0(PortInfo, 1);
    portinfo->src_address = src_address;
    portinfo->inet_addr = inet_addr;
    portinfo->port = inet_port;

    tmp = g_inet_address_to_string(inet_addr);
    g_print ("Packet from new client %s:%d\n", tmp, inet_port);
    g_free (tmp);

    listen_on_socket(0, (GSourceFunc)(receive_clock_packet), portinfo);
    ports = g_list_prepend(ports, portinfo);
  }

  /* And send the packet to the master clock provider */
  g_socket_send_to (masterSocket, serverAddress, (const gchar *) buf,
      GST_NET_TIME_PACKET_SIZE, NULL, NULL);
}

static void
listen_on_socket(gint listenPort, GSourceFunc handler, gpointer user_data)
{
  GInetAddress *localAddress;
  GSocketAddress *localSocketAddress;
  GSource *source;

  localAddress = g_inet_address_new_from_string("0.0.0.0");
  localSocketAddress = g_inet_socket_address_new(localAddress, listenPort);
  masterSocket = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, NULL);
  g_socket_bind (masterSocket, localSocketAddress, FALSE, NULL);

  source = g_socket_create_source (masterSocket, G_IO_IN, NULL);
  g_source_set_callback (source, handler, user_data, NULL);
  g_source_attach(source, NULL);

  g_socket_listen (masterSocket, NULL);
}

static GInetAddress *
make_inet_address(const gchar *hostname)
{
  GResolver *r;
  GList *result;
  GInetAddress *addr;
  r = g_resolver_get_default();
  result = g_resolver_lookup_by_name(r, hostname, NULL, NULL);
  g_object_unref(r);
  if (result == NULL)
    return NULL;
  addr = g_object_ref (result->data);
  g_resolver_free_addresses(result);
  return addr;
}

int
main(int argc, char **argv)
{
  guint signal_watch_id;
  gchar *server;
  gint local_clock_port, server_clock_port;
  GInetAddress *server_inet_addr;

  if (argc < 4) {
    g_print ("Usage %s <localport> <server> <serverport>\n  Listen on port <localport> and forward to <server>:<serverport>\n", argv[0]);
    return 1;
  }

  local_clock_port = atoi(argv[1]);
  server = argv[2];
  server_clock_port = atoi(argv[3]);

#ifdef G_OS_UNIX
  signal_watch_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, NULL);
#endif
  loop = g_main_loop_new(NULL, FALSE);

  listen_on_socket(local_clock_port, (GSourceFunc)(receive_clock_packet), NULL);
  server_inet_addr = make_inet_address(server);
  if (server_inet_addr == NULL) {
    g_print ("Failed to resolve hostname %s\n", server);
    goto done;
  }
  serverAddress = g_inet_socket_address_new(server_inet_addr, server_clock_port);
  g_object_unref(server_inet_addr);

  g_main_loop_run(loop);  

done:
  g_source_remove (signal_watch_id);
  g_main_loop_unref (loop);

  return 0;
}
