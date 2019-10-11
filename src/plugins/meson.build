plugin_sources = [ 'gstodas.c' ]

gstohmdplugin = library('gstohmdplugin',
  plugin_sources,
  include_directories: aurena_inc,
  dependencies : [gst_dep, gstaudio_dep ],
  install : true,
  install_dir : plugins_install_dir,
)

