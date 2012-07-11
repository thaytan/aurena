SUBDIRS=daemon client

bin_PROGRAMS = sonarea
sonarea_SOURCES = main.c
sonarea_CPPFLAGS = $(SNRA_CFLAGS) $(EXTRA_CFLAGS)
sonarea_LDADD = daemon/libsnra_daemon.la $(SNRA_LIBS)
