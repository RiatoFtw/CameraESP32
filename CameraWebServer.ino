#include "esp_camera.h"
#include <WiFi.h>
#include "FS.h"
#include "SD_MMC.h"
#include <time.h>

#include "board_config.h"

const char *ssid = "ZTE_2.4G_3Nf74C";
const char *password = "D5MQKdyX";

void startCameraServer();
void setupLedFlash();

void startRecording(int hours);
void stopRecording();
static void recordVideoTask(void *arg);

// ============================
// RECORDING STATE
// ============================
typedef struct {
  bool isRecording;
  bool shouldStop;
  bool continuousMode;
  unsigned long startTime;
  int duration; // in minutes
  char currentFilename[64];
  TaskHandle_t recordTaskHandle;
} RecordingState;

RecordingState recordingState = {
  .isRecording = false,
  .shouldStop = false,
  .continuousMode = false,
  .startTime = 0,
  .duration = 60,
  .currentFilename = {0},
  .recordTaskHandle = NULL
};

// ============================
// SETUP
// ============================
void setup() {
  Serial.printf("PSRAM: %s\n", psramFound() ? "YES" : "NO");
  Serial.begin(115200);
  Serial.println();

  // ============================
  // CAMERA CONFIG
  // ============================
  camera_config_t config;

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 15;
    config.fb_count = 1;
  }

  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed");
    return;
  }

  Serial.println("Camera initialized");

  // ============================
  // WIFI
  // ============================
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");
  Serial.println(WiFi.localIP());

  // ============================
  // TIME SYNC (for filenames)
  // ============================
  configTime(8 * 3600, 0, "pool.ntp.org"); // Philippines UTC+8

  // ============================
  // SD CARD INIT
  // ============================
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD Card Mount Failed");
    return;
  }

  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %llu MB\n", cardSize);

  startCameraServer();

  Serial.println("Camera Ready!");
}

void loop() {
  delay(10000);
}

// ============================
// RECORDING TASK
// ============================
static void recordVideoTask(void *arg) {
  // Get recording parameters
  int durationMinutes = recordingState.duration;
  bool continuous = recordingState.continuousMode;

  do {
    time_t now;
    time(&now);

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char filename[64];
    strftime(filename, sizeof(filename), "/video_%Y%m%d_%H%M%S.mjpeg", &timeinfo);
    strcpy(recordingState.currentFilename, filename);

    File file = SD_MMC.open(filename, FILE_WRITE);
    if (!file) {
      Serial.println("File open failed");
      recordingState.isRecording = false;
      vTaskDelete(NULL);
      return;
    }

    Serial.printf("Recording: %s (Duration: %d minutes)\n", filename, durationMinutes);
    recordingState.isRecording = true;
    recordingState.shouldStop = false;
    recordingState.startTime = millis();

    unsigned long duration = (unsigned long)durationMinutes * 60000UL; // Convert minutes to ms
    unsigned long startTime = millis();

    // Recording loop
    while (millis() - startTime < duration && !recordingState.shouldStop) {
      camera_fb_t *fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("Frame capture failed");
        delay(100);
        continue;
      }

      file.write(fb->buf, fb->len);
      esp_camera_fb_return(fb);

      delay(100); // ~10 FPS for MJPEG
    }

    file.close();
    Serial.printf("Recording saved: %s\n", filename);

    recordingState.isRecording = false;
    recordingState.shouldStop = false;

    // If continuous mode and not explicitly stopped, restart
    if (continuous && !recordingState.shouldStop) {
      Serial.println("Restarting continuous recording...");
      delay(1000); // 1 second delay between files
    }

  } while (continuous && !recordingState.shouldStop);

  recordingState.isRecording = false;
  recordingState.recordTaskHandle = NULL;
  vTaskDelete(NULL);
}

// ============================
// START RECORDING
// ============================
void startRecording(int minutes, bool continuous) {
  if (recordingState.isRecording) {
    Serial.println("Already recording");
    return;
  }

  recordingState.duration = minutes;
  recordingState.continuousMode = continuous;
  recordingState.shouldStop = false;

  xTaskCreate(
    recordVideoTask,
    "recordTask",
    8192,
    NULL,
    1,
    &recordingState.recordTaskHandle
  );
}

// ============================
// STOP RECORDING
// ============================
void stopRecording() {
  if (!recordingState.isRecording) {
    Serial.println("Not currently recording");
    return;
  }

  Serial.println("Stop recording signal sent");
  recordingState.shouldStop = true;
  recordingState.continuousMode = false;
}

// ============================
// GET RECORDING STATUS
// ============================
bool isRecording() {
  return recordingState.isRecording;
}

int getRecordingDuration() {
  return recordingState.duration;
}

bool isContinuousMode() {
  return recordingState.continuousMode;
}

unsigned long getElapsedTime() {
  if (!recordingState.isRecording) return 0;
  return millis() - recordingState.startTime;
}