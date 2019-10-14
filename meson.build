project('aurena', 'c',
  default_options: 'c_std=gnu99', version : '1.0.0',
  meson_version : '>= 0.34.0')

gst_req = '>= 1.0.0'

gst_dep = dependency('gstreamer-1.0', version : gst_req, required : false)
gstnet_dep = dependency('gstreamer-net-1.0', version : gst_req, required : false)
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req, required : false)
gstapp_dep = dependency('gstreamer-app-1.0', version : gst_req, required : false)
gstaudio_dep = dependency('gstreamer-audio-1.0', version : gst_req, required : false)
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req, required : false)
gstrtsp_dep = dependency('gstreamer-rtsp-server-1.0', version : gst_req, required : false)

if not gst_dep.found() or not gstbase_dep.found() or \
  not gstvideo_dep.found() or not gstnet_dep.found() or not gstrtsp_dep.found() or \
  not gstapp_dep.found() or not gstaudio_dep.found()
  error('''
        You need to install or upgrade the GStreamer development
        packages on your system. On debian-based systems these are
        libgstreamer1.0-dev and libgstreamer-plugins-base1.0-dev.
        on RPM-based systems gstreamer1.0-devel, libgstreamer1.0-devel
        or similar. The minimum version required is ''' + gst_req)
endif

json_glib_dep = dependency('json-glib-1.0', required : true)
libsoup_dep = dependency('libsoup-2.4', version: '> 2.48', required : true)
sqlite3 = dependency('sqlite3', version: '>=3.3', required : false)
avahi_client_dep = dependency ('avahi-client', version: '>=0.6.24')
avahi_glib_dep = dependency ('avahi-glib', version: '>=0.6.24')

plugins_install_dir = '@0@/gstreamer-1.0'.format(get_option('libdir'))

cc = meson.get_compiler('c')

config = configuration_data()
config.set('PACKAGE', '"aurena"')
config.set('VERSION', '"@0@"'.format(meson.project_version()))
config.set('PACKAGE_VERSION', '"@0@"'.format(meson.project_version()))
config.set('PACKAGE_NAME', '"Aurena"')
config.set('PACKAGE_URL', '"https://aurena.noraisin.net/"')
config.set('GST_LICENSE', '"GPL"')

add_global_arguments('-DHAVE_CONFIG_H', language : 'c')
aurena_inc = include_directories(['.', 'src'])

odas_proj = subproject('odas')
odas_dep = odas_proj.get_variable('odas_dep')

libconfig_dep = dependency('libconfig')
libm_dep = cc.find_library('m', required : false)

quiet_proj = subproject('quiet')
quiet_dep = quiet_proj.get_variable('quiet_dep')

subdir('src')

configure_file(output : 'config.h', configuration : config)
