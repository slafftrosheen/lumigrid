idf_component_register(
    SRCS "webdav.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server vfs
)
