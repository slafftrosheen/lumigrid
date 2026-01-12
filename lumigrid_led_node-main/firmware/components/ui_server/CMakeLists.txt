idf_component_register(SRCS "ui_server.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server)

# Note: Binary data embedding disabled to avoid conflicts with rest_api component
# The ui_server.c file now uses weak symbols and fallback content
# Main UI is served by the rest_api component