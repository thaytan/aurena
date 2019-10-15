odas_plugin_sources = [ 'gstodas.c',
  'odas-configs.c', 'odas-parameters.c', 'odas-objects.c'
]

gstodasplugin = library('gstodas',
  odas_plugin_sources,
  include_directories: aurena_inc,
  dependencies : [gst_dep, gstaudio_dep, odas_dep, libconfig_dep, libm_dep ],
  install : true,
  install_dir : plugins_install_dir,
)


profiles_dir = get_option('prefix') / get_option('datadir') / 'quiet'
quiet_c_args = '-DQUIET_PROFILES_PATH="' + profiles_dir / 'quiet-profiles.json"'

quiet_plugin_sources = [ 'gstquietplugin.c',
  'gstquietenc.c', 'gstquietdec.c' ]

gstquietplugin = library('gstquiet',
  quiet_plugin_sources,
  c_args: quiet_c_args,
  include_directories: aurena_inc,
  dependencies : [gst_dep, gstaudio_dep, quiet_dep ],
  install : true,
  install_dir : plugins_install_dir,
)
