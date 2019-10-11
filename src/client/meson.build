
sources = [ 'aur-client.c' ]
deps = [ gst_dep, gstnet_dep, json_glib_dep, libsoup_dep, avahi_client_dep, avahi_glib_dep ]

aurena_client_lib = static_library(
        'aurena_client',
        sources,
        dependencies: deps,
        include_directories: aurena_inc,                                                                                                                                                                        
        install: false,
)
