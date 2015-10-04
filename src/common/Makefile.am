noinst_LTLIBRARIES        = libaurena_common.la

libaurena_common_la_SOURCES = \
  aur-component.c \
  aur-component-link.c \
  aur-config.c \
  aur-event.c \
  aur-json.c \
  aur-websocket-parser.c

noinst_HEADERS = \
  aur-types.h \
  aur-component.h \
  aur-component-link.h \
  aur-config.h \
  aur-event.h \
  aur-json.h \
  aur-websocket-parser.h

libaurena_common_la_CPPFLAGS = -I$(top_srcdir)/src $(AUR_COMMON_CFLAGS) $(GST_CFLAGS) $(EXTRA_CFLAGS)
libaurena_common_la_LIBADD  = 
