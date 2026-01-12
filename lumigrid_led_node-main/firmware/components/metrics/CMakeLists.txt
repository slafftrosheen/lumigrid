idf_component_register(
    SRCS "metrics.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server heap nodesync
)
