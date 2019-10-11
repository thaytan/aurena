subdir('common')
subdir('server')
subdir('client')

executable(
        'aurena-server',
        'aurena-server.c',
        include_directories: aurena_inc,
        link_with: [aurena_server_lib, aurena_common_lib],
        dependencies: [gst_dep, gstrtsp_dep, libsoup_dep, json_glib_dep ],
        install: true,
)
