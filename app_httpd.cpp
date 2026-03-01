// Update recording control handlers

// Updated function declarations
extern void startRecording(int minutes, bool continuous); extern void stopRecording(); extern bool isRecording(); extern int getRecordingDuration(); extern unsigned long getElapsedTime();

// New handlers
static esp_err_t recording_start_handler(httpd_req_t *req) {
  char *buf = NULL;
  char _minutes[32];
  char _continuous[32];

  if (parse_get(req, &buf) != ESP_OK) {
    return ESP_FAIL;
  }
  
  int minutes = 60; // default
  bool continuous = false;
  
  if (httpd_query_key_value(buf, "minutes", _minutes, sizeof(_minutes)) == ESP_OK) {
    minutes = atoi(_minutes);
    if (minutes < 1) minutes = 1;
    if (minutes > 1440) minutes = 1440; // max 24 hours
  }
  
  if (httpd_query_key_value(buf, "continuous", _continuous, sizeof(_continuous)) == ESP_OK) {
    continuous = (atoi(_continuous) == 1);
  }
  
  free(buf);
  
  log_i("Starting recording: %d minutes, continuous: %s", minutes, continuous ? "yes" : "no");
  startRecording(minutes, continuous);
  
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, "{\"status\":\"ok\",\"message\":\"Recording started\"}", -1);
  return ESP_OK;
}

static esp_err_t recording_stop_handler(httpd_req_t *req) {
  log_i("Stopping recording");
  stopRecording();
  
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, "{\"status\":\"ok\",\"message\":\"Recording stopped\"}", -1);
  return ESP_OK;
}

static esp_err_t recording_status_handler(httpd_req_t *req) {
  static char json_response[256];
  
  bool recording = isRecording();
  int duration = getRecordingDuration();
  unsigned long elapsed = getElapsedTime();
  
  snprintf(json_response, sizeof(json_response), 
    "{\"isRecording\":%s,\"duration\":%d,\"elapsed\":%lu,\"remaining\":%lu}",
    recording ? "true" : "false",
    duration,
    elapsed / 1000,
    recording ? (duration * 60 - elapsed / 1000) : 0
  );
  
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_response, -1);
  return ESP_OK;
}

// New URI handlers
httpd_uri_t recording_start_uri = {
  .uri = "/recording/start",
  .method = HTTP_GET,
  .handler = recording_start_handler,
  .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
  ,
  .is_websocket = true,
  .handle_ws_control_frames = false,
  .supported_subprotocol = NULL
#endif
};

httpd_uri_t recording_stop_uri = {
  .uri = "/recording/stop",
  .method = HTTP_GET,
  .handler = recording_stop_handler,
  .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
  ,
  .is_websocket = true,
  .handle_ws_control_frames = false,
  .supported_subprotocol = NULL
#endif
};

httpd_uri_t recording_status_uri = {
  .uri = "/recording/status",
  .method = HTTP_GET,
  .handler = recording_status_handler,
  .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
  ,
  .is_websocket = true,
  .handle_ws_control_frames = false,
  .supported_subprotocol = NULL
#endif
};

// Registering the new handlers
httpd_register_uri_handler(camera_httpd, &recording_start_uri);
httpd_register_uri_handler(camera_httpd, &recording_stop_uri);
httpd_register_uri_handler(camera_httpd, &recording_status_uri);

// Update max_uri_handlers
config.max_uri_handlers = 19; // updated from 16 to 19