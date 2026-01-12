idf_component_register(
    SRCS "capport.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_netif lwip rest_api
)

# Embed setup wizard HTML
target_add_binary_data(${COMPONENT_LIB} "setup_wizard.html" TEXT)

