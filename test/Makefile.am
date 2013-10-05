noinst_PROGRAMS = clock-server clock-receiver

clock_server_CPPFLAGS = -I$(top_srcdir) $(GST_CFLAGS) $(SNRA_COMMON_CFLAGS) $(EXTRA_CFLAGS)
clock_server_LDADD = $(GST_LIBS)
clock_server_SOURCES = clock-server.c

clock_receiver_CPPFLAGS = -I$(top_srcdir) $(GST_CFLAGS) $(SNRA_COMMON_CFLAGS) $(EXTRA_CFLAGS)
clock_receiver_LDADD = $(GST_LIBS) 
clock_receiver_SOURCES = clock-receiver.c
