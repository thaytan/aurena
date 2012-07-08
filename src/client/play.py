#!/usr/bin/env python
import sys

import gobject
gobject.threads_init()

import glib

import pygst
pygst.require('0.10')
import gst

import json
import urllib
import socket

ml = None
def rtp_child_added (src, elem):
    if elem.get_name().startswith("rtpbin"):
        print "Setting ntp-sync"
        elem.set_property("ntp-sync", True)
        elem.set_property("use-pipeline-clock", True)

def handle_src_element(pb, src):
    if src.get_name().startswith("rtsp"):
      src.set_property("buffer-mode", 0)
      src.connect ("element-added", rtp_child_added)

def on_message(bus, message):
    global ml
    t = message.type
    if t == gst.MESSAGE_ERROR:
        err, debug = message.parse_error()
        print "Error: %s" % err, debug
        ml.quit()
    elif t == gst.MESSAGE_EOS:
        print "EOS"
        ml.quit()

def clock_update(clock, base_time):
    t= clock.get_time()
    print "Now %d (%d)" % (t, t - base_time)
    return True

def main(args):
    _, server = args

    u = urllib.urlopen('http://' + server + ':5457/control')

    ctx = json.load(u)
    port = ctx['clock-port']
    base_time = ctx['base-time']
    media_uri = '%s://%s:%d%s' % (ctx['resource-protocol'], server, ctx['resource-port'], ctx['resource-path'])

    # print ctx

    # make a clock slaving to the network
    ip = socket.gethostbyname(server)
    clock = gst.NetClientClock(None, ip, port, ctx['current-time'])

    # make the pipeline
    pipeline = gst.parse_launch('playbin2 name=pb');
    pipeline.set_property("uri", media_uri) # uri interface

    pipeline.connect ("source-setup", handle_src_element)

    # disable the pipeline's management of base_time -- we're going
    # to set it ourselves.
    pipeline.set_new_stream_time(gst.CLOCK_TIME_NONE)

    # use it in the pipeline
    pipeline.set_base_time(base_time)
    pipeline.use_clock(clock)

    # now we go :)
    print "Starting playback of %s with base_time %d\n" % (media_uri, base_time)
    pipeline.set_state(gst.STATE_PLAYING)

    bus = pipeline.get_bus()
    bus.enable_sync_message_emission()
    bus.add_signal_watch()
    bus.connect('message', on_message)

    glib.timeout_add_seconds(5000, clock_update, clock, base_time)
    # wait until things stop
    global ml
    try:
        ml = glib.MainLoop()
        ml.run()
    except:
        pass

    pipeline.set_state(gst.STATE_NULL)

if __name__ == '__main__':
    main(sys.argv)
