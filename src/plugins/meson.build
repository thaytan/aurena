plugin_sources = [ 'gstodas.c',
  'odas-configs.c', 'odas-parameters.c', 'odas-objects.c'
]

gstodasplugin = library('gstodasplugin',
  plugin_sources,
  include_directories: aurena_inc,
  dependencies : [gst_dep, gstaudio_dep, odas_dep, libconfig_dep, libm_dep ],
  install : true,
  install_dir : plugins_install_dir,
)

