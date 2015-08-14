bin_PROGRAMS = aurena-mic-send aurena-mic-receive

aurena_mic_send_CPPFLAGS = -I$(top_srcdir) $(AUR_COMMON_CFLAGS) $(GST_RTSP_CFLAGS) $(GST_CFLAGS) $(EXTRA_CFLAGS)
aurena_mic_send_LDADD = $(AUR_COMMON_LIBS) $(GST_RTSP_LIBS) $(GST_LIBS)
aurena_mic_send_SOURCES = aurena-mic-send.c

aurena_mic_receive_CPPFLAGS = -I$(top_srcdir) $(AUR_COMMON_CFLAGS) $(GST_RTSP_CFLAGS) $(GST_CFLAGS) $(EXTRA_CFLAGS)
aurena_mic_receive_LDADD = $(AUR_COMMON_LIBS) $(GST_RTSP_LIBS) $(GST_LIBS)
aurena_mic_receive_SOURCES = aurena-mic-receive.c

