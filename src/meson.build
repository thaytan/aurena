subdir('common')
subdir('server')
subdir('client')
subdir('plugins')

executable(
        'aurena-server',
        'aurena-server.c',
        include_directories: aurena_inc,
        link_with: [aurena_server_lib, aurena_common_lib],
        dependencies: [gst_dep, gstrtsp_dep, libsoup_dep, json_glib_dep ],
        install: true,
)

executable(
        'aurena-simple-client',
        'simple-client.c',
        include_directories: aurena_inc,
        link_with: [aurena_client_lib, aurena_common_lib],
        dependencies: [gst_dep, gstrtsp_dep, libsoup_dep, json_glib_dep ],
        install: true,
)

