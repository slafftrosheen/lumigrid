#include "ui_server.h"
#include "esp_http_server.h"

// Use weak symbols to avoid conflicts with rest_api component
// If the binary data is not provided, use simple redirects
extern const uint8_t _binary_index_html_start[] __attribute__((weak));
extern const uint8_t _binary_index_html_end[] __attribute__((weak));
extern const uint8_t _binary_main_js_start[] __attribute__((weak));
extern const uint8_t _binary_main_js_end[] __attribute__((weak));
extern const uint8_t _binary_style_css_start[] __attribute__((weak));
extern const uint8_t _binary_style_css_end[] __attribute__((weak));

static const char* fallback_html = "<html><head><meta http-equiv=\"refresh\" content=\"0;url=/api\"/></head><body>Redirecting...</body></html>";
static const char* fallback_js = "// UI served by REST API component";
static const char* fallback_css = "/* UI served by REST API component */";

static esp_err_t ui_index(httpd_req_t *r){ 
    httpd_resp_set_type(r,"text/html");
    if (&_binary_index_html_start && &_binary_index_html_end) {
        httpd_resp_send(r,(const char*)_binary_index_html_start,_binary_index_html_end-_binary_index_html_start);
    } else {
        httpd_resp_send(r, fallback_html, -1);
    }
    return ESP_OK;
}

static esp_err_t ui_js(httpd_req_t *r){
    httpd_resp_set_type(r,"application/javascript");
    if (&_binary_main_js_start && &_binary_main_js_end) {
        httpd_resp_send(r,(const char*)_binary_main_js_start,_binary_main_js_end-_binary_main_js_start);
    } else {
        httpd_resp_send(r, fallback_js, -1);
    }
    return ESP_OK;
}

static esp_err_t ui_css(httpd_req_t *r){
    httpd_resp_set_type(r,"text/css");
    if (&_binary_style_css_start && &_binary_style_css_end) {
        httpd_resp_send(r,(const char*)_binary_style_css_start,_binary_style_css_end-_binary_style_css_start);
    } else {
        httpd_resp_send(r, fallback_css, -1);
    }
    return ESP_OK;
}

esp_err_t ui_server_attach(httpd_handle_t server){
    // existing endpoint registrations...
    static const httpd_uri_t U_INDEX = { .uri="/",        .method=HTTP_GET, .handler=ui_index };
    static const httpd_uri_t U_I     = { .uri="/ui/",     .method=HTTP_GET, .handler=ui_index };
    static const httpd_uri_t U_IDX   = { .uri="/ui/index.html", .method=HTTP_GET, .handler=ui_index };
    static const httpd_uri_t U_JS    = { .uri="/ui/main.js",    .method=HTTP_GET, .handler=ui_js };
    static const httpd_uri_t U_CSS   = { .uri="/ui/style.css",  .method=HTTP_GET, .handler=ui_css };
    httpd_register_uri_handler(server, &U_INDEX);
    httpd_register_uri_handler(server, &U_I);
    httpd_register_uri_handler(server, &U_IDX);
    httpd_register_uri_handler(server, &U_JS);
    httpd_register_uri_handler(server, &U_CSS);
    return ESP_OK;
}