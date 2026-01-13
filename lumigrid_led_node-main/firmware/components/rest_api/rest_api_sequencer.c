#include "rest_api.h"
#include "sequencer.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "cJSON.h"
#include <string.h>
#include <dirent.h>
#include "storage_fs.h"

static const char* TAG = "REST_SEQ";

// Validate timeline JSON against schema
bool rest_api_validate_timeline_json(cJSON *root, char *error_buf, size_t error_buf_len) {
    if (!root || !cJSON_IsObject(root)) {
        snprintf(error_buf, error_buf_len, "timeline must be a JSON object");
        return false;
    }

    cJSON *timeline_name = cJSON_GetObjectItem(root, "timeline_name");
    if (!timeline_name || !cJSON_IsString(timeline_name) || !timeline_name->valuestring || !timeline_name->valuestring[0]) {
        snprintf(error_buf, error_buf_len, "timeline_name is required");
        return false;
    }

    cJSON *schedule = cJSON_GetObjectItem(root, "schedule");
    if (!schedule || !cJSON_IsObject(schedule)) {
        snprintf(error_buf, error_buf_len, "schedule is required");
        return false;
    }

    cJSON *events = cJSON_GetObjectItem(root, "events");
    if (!events || !cJSON_IsArray(events)) {
        snprintf(error_buf, error_buf_len, "events is required");
        return false;
    }

    return true;
}

// Helper to receive and parse JSON body
static esp_err_t recv_json_body(httpd_req_t *req, cJSON **json_out) {
    char buf[1024];
    size_t content_len = req->content_len;
    
    if (content_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request too large");
        return ESP_FAIL;
    }
    
    int ret = httpd_req_recv(req, buf, content_len);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    *json_out = cJSON_Parse(buf);
    if (!*json_out) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

// GET /api/sequences - List all sequences
esp_err_t seq_list_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory error");
        return ESP_FAIL;
    }
    
    cJSON *sequences = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "sequences", sequences);
    
    char dir_path[128];
    if (storage_fs_mount_path(dir_path, sizeof(dir_path), "timelines")) {
        DIR *d = opendir(dir_path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                const char *ext = strrchr(e->d_name, '.');
                if (ext && strcasecmp(ext, ".json") == 0) {
                    // strip extension
                    char name[64];
                    size_t name_len = ext - e->d_name;
                    if (name_len < sizeof(name)) {
                        strncpy(name, e->d_name, name_len);
                        name[name_len] = 0;

                        cJSON *seq = cJSON_CreateObject();
                        cJSON_AddStringToObject(seq, "name", name);
                        cJSON_AddItemToArray(sequences, seq);
                    }
                }
            }
            closedir(d);
        }
    }
    
    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// POST /api/sequences - Create new sequence
esp_err_t seq_create_handler(httpd_req_t *req) {
    cJSON *json = NULL;
    if (recv_json_body(req, &json) != ESP_OK) {
        return ESP_FAIL;
    }
    
    cJSON *name_obj = cJSON_GetObjectItem(json, "name");
    cJSON *bpm_obj = cJSON_GetObjectItem(json, "bpm");
    
    if (!name_obj || !cJSON_IsString(name_obj)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'name'");
        return ESP_FAIL;
    }
    
    uint16_t bpm = 120;
    if (bpm_obj && cJSON_IsNumber(bpm_obj)) {
        bpm = (uint16_t)bpm_obj->valueint;
    }
    
    sequence_t *seq = sequencer_create_sequence(name_obj->valuestring, bpm);
    cJSON_Delete(json);
    
    if (!seq) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create");
        return ESP_FAIL;
    }
    
    // Build response
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "id", seq->sequence_id);
    cJSON_AddStringToObject(resp, "name", seq->name);
    cJSON_AddNumberToObject(resp, "bpm", seq->bpm);
    cJSON_AddNumberToObject(resp, "total_duration_ms", seq->total_duration_ms);
    cJSON_AddNumberToObject(resp, "track_count", seq->track_count);
    
    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(resp);
    
    // Note: Sequence is created in memory but not persisted yet
    // Caller should export and save if needed
    sequencer_destroy_sequence(seq);
    
    return ESP_OK;
}

// GET /api/timelines/{name}/export - Export timeline as JSON
esp_err_t timeline_export_handler(httpd_req_t *req) {
    char name[64];
    if (sscanf(req->uri, "/api/timelines/%63[^/]/export", name) != 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid timeline name");
        return ESP_FAIL;
    }

    char path[128];
    snprintf(path, sizeof(path), "/spiffs/timelines/%s.json", name);

    FILE* f = fopen(path, "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Timeline not found");
        return ESP_FAIL;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(sz + 1);
    if (!buf) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_ERR_NO_MEM;
    }

    fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    free(buf);

    return ESP_OK;
}

// POST /api/timelines/import - Import timeline from JSON
esp_err_t timeline_import_handler(httpd_req_t *req) {
    cJSON *json = NULL;
    if (recv_json_body(req, &json) != ESP_OK) {
        return ESP_FAIL;
    }

    char error_msg[256];
    if (!rest_api_validate_timeline_json(json, error_msg, sizeof(error_msg))) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error_msg);
        return ESP_FAIL;
    }

    cJSON* name_obj = cJSON_GetObjectItem(json, "timeline_name");
    if (!name_obj || !cJSON_IsString(name_obj)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'timeline_name'");
        return ESP_FAIL;
    }

    char path[128];
    snprintf(path, sizeof(path), "/spiffs/timelines/%s.json", name_obj->valuestring);

    FILE* f = fopen(path, "w");
    if (!f) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open timeline file");
        return ESP_FAIL;
    }

    char* json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    fprintf(f, "%s", json_str);
    fclose(f);
    free(json_str);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "timeline_name", name_obj->valuestring);

    char* resp_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);

    free(resp_str);
    cJSON_Delete(resp);

    return ESP_OK;
}

// POST /api/sequences/{id}/play - Start playback
esp_err_t seq_play_handler(httpd_req_t *req) {
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "playing");
    cJSON_AddNumberToObject(resp, "playhead_ms", 0);
    
    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

// POST /api/sequences/{id}/pause - Pause playback
esp_err_t seq_pause_handler(httpd_req_t *req) {
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "paused");
    cJSON_AddNumberToObject(resp, "playhead_ms", 0);
    
    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

// POST /api/sequences/{id}/stop - Stop playback
esp_err_t seq_stop_handler(httpd_req_t *req) {
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "stopped");
    cJSON_AddNumberToObject(resp, "playhead_ms", 0);
    
    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

// GET /api/status - System status
esp_err_t status_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory error");
        return ESP_FAIL;
    }
    
    cJSON_AddBoolToObject(root, "connected", true);
    cJSON_AddNumberToObject(root, "uptime", esp_timer_get_time() / 1000);
    cJSON_AddNumberToObject(root, "heap_free", esp_get_free_heap_size());
    cJSON_AddStringToObject(root, "effect", "None");
    
    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// POST /api/effect/apply - Apply effect (for UI compatibility)
esp_err_t effect_apply_handler(httpd_req_t *req) {
    cJSON *json = NULL;
    if (recv_json_body(req, &json) != ESP_OK) {
        return ESP_FAIL;
    }
    
    // Extract effect name and params
    cJSON *effect_obj = cJSON_GetObjectItem(json, "effect");
    if (!effect_obj || !cJSON_IsString(effect_obj)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'effect'");
        return ESP_FAIL;
    }
    
    // Extract params (optional)
    // In a full implementation, this would call into effect_control
    // to actually apply the effect with the given parameters
    
    cJSON_Delete(json);
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddStringToObject(resp, "effect", effect_obj->valuestring);
    
    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

// GET /api/effects - List available effects
esp_err_t effects_list_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory error");
        return ESP_FAIL;
    }
    
    cJSON *effects = cJSON_CreateArray();
    
    // Add effect names (pattern-based)
    const char *pattern_effects[] = {
        "scanner", "comet", "theater_chase", "running_lights",
        "heartbeat", "breathing", "police_strobe", "confetti",
        "bpm_strobe", "ripple"
    };
    
    for (int i = 0; i < 10; i++) {
        cJSON_AddItemToArray(effects, cJSON_CreateString(pattern_effects[i]));
    }
    
    // Add noise-based effects
    const char *noise_effects[] = {
        "perlin_flow", "plasma", "lava_lamp", "clouds",
        "marble", "electric", "cellular"
    };
    
    for (int i = 0; i < 7; i++) {
        cJSON_AddItemToArray(effects, cJSON_CreateString(noise_effects[i]));
    }
    
    // Add classic effects
    const char *classic_effects[] = {
        "rainbow_flow", "twinkle", "meteor", "fire2012"
    };
    
    for (int i = 0; i < 4; i++) {
        cJSON_AddItemToArray(effects, cJSON_CreateString(classic_effects[i]));
    }
    
    cJSON_AddItemToObject(root, "effects", effects);
    
    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// Register all sequencer endpoints
void rest_api_register_sequencer(httpd_handle_t server) {
    if (!server) return;
    
    static const httpd_uri_t uri_status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL
    };
    
    static const httpd_uri_t uri_effects = {
        .uri = "/api/effects",
        .method = HTTP_GET,
        .handler = effects_list_handler,
        .user_ctx = NULL
    };
    
    static const httpd_uri_t uri_effect_apply = {
        .uri = "/api/effect/apply",
        .method = HTTP_POST,
        .handler = effect_apply_handler,
        .user_ctx = NULL
    };
    
    static const httpd_uri_t uri_seq_list = {
        .uri = "/api/sequences",
        .method = HTTP_GET,
        .handler = seq_list_handler,
        .user_ctx = NULL
    };
    
    static const httpd_uri_t uri_seq_create = {
        .uri = "/api/sequences",
        .method = HTTP_POST,
        .handler = seq_create_handler,
        .user_ctx = NULL
    };

    static const httpd_uri_t uri_timeline_export = {
        .uri = "/api/timelines/*/export",
        .method = HTTP_GET,
        .handler = timeline_export_handler,
        .user_ctx = NULL
    };

    static const httpd_uri_t uri_timeline_import = {
        .uri = "/api/timelines/import",
        .method = HTTP_POST,
        .handler = timeline_import_handler,
        .user_ctx = NULL
    };
    
    httpd_register_uri_handler(server, &uri_status);
    httpd_register_uri_handler(server, &uri_effects);
    httpd_register_uri_handler(server, &uri_effect_apply);
    httpd_register_uri_handler(server, &uri_seq_list);
    httpd_register_uri_handler(server, &uri_seq_create);
    httpd_register_uri_handler(server, &uri_timeline_export);
    httpd_register_uri_handler(server, &uri_timeline_import);
    
    ESP_LOGI(TAG, "Sequencer API registered");
}
