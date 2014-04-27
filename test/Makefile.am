noinst_PROGRAMS = clock-server clock-receiver clock-bouncer

clock_server_CPPFLAGS = -I$(top_srcdir) $(GST_CFLAGS) $(AUR_COMMON_CFLAGS) $(EXTRA_CFLAGS)
clock_server_LDADD = $(GST_LIBS)
clock_server_SOURCES = clock-server.c

clock_receiver_CPPFLAGS = -I$(top_srcdir) $(GST_CFLAGS) $(AUR_COMMON_CFLAGS) $(EXTRA_CFLAGS)
clock_receiver_LDADD = $(GST_LIBS) 
clock_receiver_SOURCES = clock-receiver.c

clock_bouncer_CPPFLAGS = -I$(top_srcdir) $(GST_CFLAGS) $(AUR_COMMON_CFLAGS) $(EXTRA_CFLAGS)
clock_bouncer_LDADD = $(AUR_COMMON_LIBS)
clock_bouncer_SOURCES = clock-bouncer.c
