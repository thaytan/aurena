
sources = [ 'aur-avahi.c', 'aur-client-proxy.c', 'aur-http-client.c',
    'aur-http-resource.c', 'aur-manager.c', 'aur-media-db.c',
    'aur-resource.c', 'aur-receiver.c', 'aur-receiver-ingest.c',
    'aur-receiver-processor.c', 'aur-rtsp-play-media.c', 'aur-server.c' ]
deps = [ gst_dep, gstnet_dep, gstapp_dep, gstaudio_dep, json_glib_dep, libsoup_dep, sqlite3, avahi_client_dep, avahi_glib_dep ]

aurena_server_lib = static_library(
        'aurena_server',
        sources,
        dependencies: deps,
        include_directories: aurena_inc,                                                                                                                                                                        
        install: false,
)
