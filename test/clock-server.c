#ifdef CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <gst/gst.h>
#include <gst/net/gstnet.h>

#ifdef G_OS_UNIX
#include <glib-unix.h>
#include <sys/wait.h>
#endif

GMainLoop *loop;

static gboolean
intr_handler (G_GNUC_UNUSED gpointer user_data)
{
  g_print("Exiting.\n");
  g_main_loop_quit(loop);
  return TRUE;
}

int
main(int argc, char **argv)
{
  guint signal_watch_id;
  GstClock *clock;
  GstNetTimeProvider *net_clock;
  gint clock_port;

  gst_init(&argc, &argv);

  clock = gst_system_clock_obtain ();
  net_clock = gst_net_time_provider_new (clock, NULL, 0);
  g_object_get (net_clock, "port", &clock_port, NULL);
  gst_object_unref (clock);

#ifdef G_OS_UNIX
  signal_watch_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, NULL);
#endif

  loop = g_main_loop_new(NULL, FALSE);

  g_print("Net clock provider ready on port %d.\n", clock_port);
  g_main_loop_run(loop);  

  gst_object_unref (net_clock);
  g_source_remove (signal_watch_id);

  return 0;
}
