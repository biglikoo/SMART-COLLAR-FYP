#include "Arduino.h" // CRITICAL: This bridges ESP-IDF and Arduino
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "MAX30105.h" 
#include "heartRate.h" 
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>

// ==========================================
// 🌐 NETWORK CONFIGURATION
// ==========================================
const char* WIFI_SSID     = "YOUR_HOTSPOT_NAME";
const char* WIFI_PASSWORD = "YOUR_HOTSPOT_PASSWORD";

// The local IP address of your laptop running the Python Dashboard
const char* TARGET_IP     = "192.168.1.100"; 
const int TARGET_PORT     = 8080;

WiFiUDP udp;

// ==========================================
// 📌 PIN DEFINITIONS & HARDWARE CONFIG
// ==========================================
#define PIEZO_PIN 34       
#define ONE_WIRE_BUS 4     
#define DHTPIN 5           
#define DHTTYPE DHT22      

// ==========================================
// 🧰 SENSOR OBJECTS
// ==========================================
Adafruit_MPU6050 mpu;
MAX30105 particleSensor;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
DHT dht(DHTPIN, DHTTYPE);

// ==========================================
// 🚦 FREERTOS QUEUE & TASK HANDLES
// ==========================================
QueueHandle_t sensorDataQueue;
TaskHandle_t SensorTaskHandle;
TaskHandle_t WiFiTaskHandle;

// Fixed-size character array to prevent memory fragmentation
#define MESSAGE_BUFFER_SIZE 128 

// ==========================================
// 🐢 GLOBAL VARIABLES FOR SLOW SENSORS
// ==========================================
float lastEnvTemp = 0.0;
float lastEnvHumid = 0.0;
float lastAnimalTemp = 0.0;
unsigned long lastDHTReadTime = 0;
const unsigned long DHT_INTERVAL = 2000; 

// MAX30102 Variables
long lastBeat = 0; 
float beatsPerMinute = 0;
int beatAvg = 0;
int currentSpO2 = 98; 

// ==========================================
// 🧠 CORE 0: SENSOR POLLING TASK
// ==========================================
void SensorTask(void *pvParameters) {
  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(62); // 62ms = ~16.1 Hz Nyquist Lock
  
  xLastWakeTime = xTaskGetTickCount();
  ds18b20.requestTemperatures(); 

  for (;;) {
    unsigned long currentMillis = millis();

    // 1. Read MPU-6050
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    // 2. Read Piezoelectric
    int piezoVal = analogRead(PIEZO_PIN);

    // 3. Process MAX30102 (Non-blocking)
    long irValue = particleSensor.getIR();
    if (checkForBeat(irValue) == true) {
      long delta = millis() - lastBeat;
      lastBeat = millis();
      beatsPerMinute = 60 / (delta / 1000.0);
      if (beatsPerMinute < 255 && beatsPerMinute > 20) {
        beatAvg = (beatAvg + beatsPerMinute) / 2; 
      }
    }

    // 4. Read DS18B20 (Async)
    lastAnimalTemp = ds18b20.getTempCByIndex(0);
    ds18b20.requestTemperatures(); 

    // 5. Read DHT22 (Every 2 seconds)
    if (currentMillis - lastDHTReadTime >= DHT_INTERVAL) {
      float t = dht.readTemperature();
      float h = dht.readHumidity();
      if (!isnan(t) && !isnan(h)) {
        lastEnvTemp = t;
        lastEnvHumid = h;
      }
      lastDHTReadTime = currentMillis;
    }

    // 6. Construct the CSV String securely
    char txBuffer[MESSAGE_BUFFER_SIZE];
    snprintf(txBuffer, sizeof(txBuffer), "%lu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%.2f,%.2f,%.2f",
             currentMillis, 
             a.acceleration.x, a.acceleration.y, a.acceleration.z, 
             g.gyro.x, g.gyro.y, g.gyro.z, 
             currentSpO2, beatAvg, piezoVal, 
             lastAnimalTemp, lastEnvTemp, lastEnvHumid);

    // 7. Push string to the FreeRTOS Queue
    xQueueSend(sensorDataQueue, &txBuffer, 0);

    // 8. Yield to Scheduler
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

// ==========================================
// 📡 CORE 1: WI-FI & UDP TRANSMISSION TASK
// ==========================================
void WiFiTask(void *pvParameters) {
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.print(".");
  }
  
  Serial.println("\nWi-Fi Connected!");
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());

  char rxBuffer[MESSAGE_BUFFER_SIZE];

  for (;;) {
    if (xQueueReceive(sensorDataQueue, &rxBuffer, portMAX_DELAY) == pdPASS) {
      udp.beginPacket(TARGET_IP, TARGET_PORT);
      udp.print(rxBuffer);
      udp.endPacket();
      
      // Print to Serial monitor for local debugging
      Serial.println(rxBuffer);
    }
  }
}

// ==========================================
// 🚀 ESP-IDF ENTRY POINT (Replaces setup/loop)
// ==========================================
extern "C" void app_main() {
  // 1. Boot up the Arduino Core environment inside ESP-IDF
  initArduino();

  // 2. Standard Setup Logic
  Serial.begin(115200);
  while (!Serial) vTaskDelay(10); 

  // Initialize the Queue
  sensorDataQueue = xQueueCreate(20, MESSAGE_BUFFER_SIZE);

  Wire.begin();
  Wire.setClock(100000);

  // --- Initialize MPU-6050 ---
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050");
    while (1) vTaskDelay(10);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_10_HZ); 

  // --- Initialize MAX30102 ---
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("MAX30102 not found");
    while (1) vTaskDelay(10);
  }
  particleSensor.setup(); 
  particleSensor.setPulseAmplitudeRed(0x0A); 
  particleSensor.setPulseAmplitudeGreen(0); 

  // --- Initialize DS18B20 ---
  ds18b20.begin();
  ds18b20.setWaitForConversion(false); 

  // --- Initialize DHT22 ---
  dht.begin();

  // --- Start FreeRTOS Tasks ---
  xTaskCreatePinnedToCore(
    SensorTask, "SensorTask", 8192, NULL, 1, &SensorTaskHandle, 0 
  );

  xTaskCreatePinnedToCore(
    WiFiTask, "WiFiTask", 8192, NULL, 1, &WiFiTaskHandle, 1 
  );

  // 3. Delete the main task so it doesn't waste CPU cycles (replaces the Arduino loop)
  vTaskDelete(NULL); 
}