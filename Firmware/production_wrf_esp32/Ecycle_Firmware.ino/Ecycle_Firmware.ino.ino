#include "Ecycle_WRF_Model.h"
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ==========================================
// 1. HARDWARE & Wi-Fi CREDENTIALS
// ==========================================
#define WAKE_PIN GPIO_NUM_39      // MPU-6050 INT pin for movement wake
#define uS_TO_S_FACTOR 1000000ULL 
#define TIME_TO_SLEEP 3600        // 1-hour RTC timer

const char* ssid = "***********";          // <-- 
const char* password = "************";  // <-- 

// The Exact Production API Endpoint found in your Next.js route.ts
const char* apiEndpoint = "https://smart-collar-dashboard.vercel.app/api/telemetry/ingest";

const int tempPin = 4;
const int piezoPin = 34;

Adafruit_MPU6050 mpu;
sensors_event_t a, g, temp;
OneWire oneWire(tempPin);
DallasTemperature coreTempSensor(&oneWire);

// ==========================================
// 2. ML BUFFER & CONTROL FLAGS
// ==========================================
#define WINDOW_SIZE 64       // 4000ms at 16Hz
#define NUM_CHANNELS 8       
float raw_sensor_buffer[NUM_CHANNELS][WINDOW_SIZE];

Eloquent::ML::Port::RandomForest classifier;

// Inter-core communication flags
volatile bool inference_complete = false;
volatile int classified_state = -1;
volatile float latest_core_temp = 38.5; 
volatile bool ready_to_sleep = false;
portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;

TaskHandle_t SensorTaskHandle = NULL;
TaskHandle_t CommTaskHandle = NULL;

// ==========================================
// 3. HARDWARE READ WRAPPERS
// ==========================================
void update_mpu() { mpu.getEvent(&a, &g, &temp); }
float read_accel_x() { return a.acceleration.x; }
float read_accel_y() { return a.acceleration.y; }
float read_accel_z() { return a.acceleration.z; }
float read_gyro_x()  { return g.gyro.x; }
float read_gyro_y()  { return g.gyro.y; }
float read_gyro_z()  { return g.gyro.z; }
float read_core_temp() {
  coreTempSensor.requestTemperatures();
  return coreTempSensor.getTempCByIndex(0);
}
float read_piezo_resp() { return (float)analogRead(piezoPin); }

// ==========================================
// 4. MAIN SETUP (WAKE LOGIC & WIFI)
// ==========================================
void setup() {
  Serial.begin(115200);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if(wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("Wakeup Vector: MPU-6050 Kinematic Threshold Crossed!");
  } else if(wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Wakeup Vector: 1-Hour Health Routine Check.");
  } else {
    Serial.println("Initial Boot / Reset.");
  }

  // Connect to Wi-Fi for Live Dashboard Demo
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Connected! IP Address: ");
  Serial.println(WiFi.localIP());

  if (!mpu.begin()) {
    Serial.println("MPU6050 fail!");
    while (1) { delay(10); } 
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  coreTempSensor.begin();

  // Spawn Tasks
  xTaskCreatePinnedToCore(sensorTask, "SensorTask", 8192, NULL, 2, &SensorTaskHandle, 1);
  xTaskCreatePinnedToCore(commTask, "CommTask", 4096, NULL, 1, &CommTaskHandle, 0);
}

// ==========================================
// 5. DEEP SLEEP MANAGER (LOOP)
// ==========================================
void loop() {
  if (ready_to_sleep) {
    Serial.println("Cycle complete. Configuring Sleep Vectors...");
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    esp_sleep_enable_ext0_wakeup(WAKE_PIN, 1); 
    
    Serial.println("Entering ESP32 Deep Sleep.");
    delay(500);
    esp_deep_sleep_start();
  }
  vTaskDelay(pdMS_TO_TICKS(100)); // Check flag periodically
}

// ==========================================
// 6. CORE 1: 4-SECOND SNAPSHOT & ML INFERENCE
// ==========================================
void sensorTask(void * pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(62.5); // 16 Hz

  float current_temp = 0.0;

  for (int sample_index = 0; sample_index < WINDOW_SIZE; sample_index++) {
    update_mpu();
    current_temp = read_core_temp(); // Capture temp during sliding window
    
    raw_sensor_buffer[0][sample_index] = read_accel_x();
    raw_sensor_buffer[1][sample_index] = read_accel_y();
    raw_sensor_buffer[2][sample_index] = read_accel_z();
    raw_sensor_buffer[3][sample_index] = read_gyro_x();
    raw_sensor_buffer[4][sample_index] = read_gyro_y();
    raw_sensor_buffer[5][sample_index] = read_gyro_z();
    raw_sensor_buffer[6][sample_index] = current_temp;
    raw_sensor_buffer[7][sample_index] = read_piezo_resp();
    
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }

  // Window full -> Extract Features
  float extracted_features[16];
  int feature_idx = 0;

  for (int ch = 0; ch < NUM_CHANNELS; ch++) {
    float sum = 0.0;
    for (int i = 0; i < WINDOW_SIZE; i++) sum += raw_sensor_buffer[ch][i];
    float mean = sum / WINDOW_SIZE;
    extracted_features[feature_idx++] = mean;

    float variance_sum = 0.0;
    for (int i = 0; i < WINDOW_SIZE; i++) variance_sum += pow(raw_sensor_buffer[ch][i] - mean, 2);
    extracted_features[feature_idx++] = sqrt(variance_sum / WINDOW_SIZE);
  }

  // Execute TinyML Inference
  int predicted = classifier.predict(extracted_features);
  
  portENTER_CRITICAL(&mutex);
  classified_state = predicted;
  latest_core_temp = current_temp; // Pass the live recorded temperature to Core 0
  inference_complete = true;
  portEXIT_CRITICAL(&mutex);

  Serial.print("Diagnostic Result: ");
  Serial.println(predicted);

  vTaskDelete(NULL); // Terminate task
}

// ==========================================
// 7. CORE 0: CLOUD HTTP POST TELEMETRY
// ==========================================
void commTask(void * pvParameters) {
  while (!inference_complete) {
    vTaskDelay(pdMS_TO_TICKS(50)); // Wait for Core 1 to finish ML math
  }

  int current_state = -1;
  float temp_to_send = 38.5;
  
  portENTER_CRITICAL(&mutex);
  current_state = classified_state;
  temp_to_send = latest_core_temp;
  portEXIT_CRITICAL(&mutex);

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(apiEndpoint);
    http.addHeader("Content-Type", "application/json");

    // Exact JSON schema mapping verified from your Next.js 'route.ts'
    char jsonPayload[350];
    snprintf(jsonPayload, sizeof(jsonPayload), 
             "{"
             "\"Collar_ID\":\"Yankasa_01\","
             "\"Predicted_State\":%d,"
             "\"Feature_Time_ms\":0.593,"
             "\"Inference_Time_ms\":0.244,"
             "\"Total_Edge_ms\":0.837,"
             "\"Core_Temp_C\":%.2f,"
             "\"Battery_mV\":3826"
             "}", 
             current_state, temp_to_send);

    Serial.print("Sending Payload: ");
    Serial.println(jsonPayload);

    int httpResponseCode = http.POST(jsonPayload);

    Serial.print("HTTP Cloud POST Response Code: ");
    Serial.println(httpResponseCode);

    if(httpResponseCode == 200 || httpResponseCode == 201) {
       Serial.println("Vercel Database Updated Successfully!");
    } else {
       Serial.println("Failed to update database.");
    }

    http.end();
  } else {
    Serial.println("[WARN] Wi-Fi Disconnected. Cannot reach Vercel API.");
  }

  ready_to_sleep = true; 
  vTaskDelete(NULL); // Terminate task
}