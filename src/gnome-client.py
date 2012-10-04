#!/usr/bin/env python
# Copyright Ian McKellar 2012 (adapted from linux-rdio)

from gi.repository import GLib, Gio, GdkPixbuf, Gtk, Soup, JSCore, WebKit
import os

class AurenaView(WebKit.WebView):
    def __init__(self):
        WebKit.WebView.__init__(self)

        # scale other content besides from text as well
        self.set_full_content_zoom(True)

        self.load_uri('http://localhost:5457/ui/')

class AurenaWindow(Gtk.Window):
    def __init__(self):
        Gtk.Window.__init__(self)
        self.set_default_size(700, 350)
        self.set_title('Aurena')

        self.web_view = AurenaView()

        self.scrolled_window = Gtk.ScrolledWindow()
        self.scrolled_window.add(self.web_view)
        self.scrolled_window.show_all()

        self.add(self.scrolled_window)

        self.web_view.connect('title-changed', self.title_changed)

        # TODO: async?
        icon_file = Gio.File.new_for_uri('http://localhost:5457/ui/icon.png')
        icon = GdkPixbuf.Pixbuf.new_from_stream(icon_file.read(None), None)
        self.set_icon(icon)

        # Set up for media key handling
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        self.proxy = Gio.DBusProxy.new_sync(bus, 0, None, 'org.gnome.SettingsDaemon',
            '/org/gnome/SettingsDaemon/MediaKeys', 'org.gnome.SettingsDaemon.MediaKeys', None)
        self.proxy.call_sync('GrabMediaPlayerKeys', GLib.Variant('(su)', ('Aurena', 0)), 0, -1, None)
        self.proxy.connect('g-signal', self.mediakey_signal)

    def mediakey_signal(self, proxy, sender, signal, parameters):
        if signal != 'MediaPlayerKeyPressed':
            return
        key = parameters.get_child_value(1).get_string()
        if key == 'Play':
            self.web_view.execute_script('aurena.playPause()')
        if key == 'Next':
            self.web_view.execute_script('aurena.next()')
        if key == 'Previous':
            self.web_view.execute_script('aurena.previous()')

    def title_changed(self, view, frame, title):
        if frame == view.get_main_frame():
            self.set_title(title)

# make a data dir for this app
data_dir = os.path.join(GLib.get_user_data_dir(), 'aurena')
if not os.path.exists(data_dir):
    os.makedirs(data_dir)

# put cookies in it
cookie_jar = os.path.join(data_dir, 'cookies.txt')
jar = Soup.CookieJarText(filename=cookie_jar)
session = WebKit.get_default_session()
session.add_feature(jar)

w = AurenaWindow()
w.show()
w.connect('destroy', lambda w: Gtk.main_quit())

Gtk.main()
