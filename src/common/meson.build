
sources = [ 'aur-component.c', 'aur-component-link.c', 'aur-config.c',
  'aur-event.c', 'aur-json.c', 'aur-websocket-parser.c' ]

aurena_common_lib = static_library(
        'aurena_common',
        sources,
        dependencies: [ gst_dep, json_glib_dep, libsoup_dep, sqlite3 ],
        include_directories: aurena_inc,
        install: false,
)
