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
// 🌐 NETWORK CONFIGURATION (USER EDIT REQUIRED)
// ==========================================
const char* WIFI_SSID     = "YOUR_HOTSPOT_NAME";
const char* WIFI_PASSWORD = "YOUR_HOTSPOT_PASSWORD";

// Dynamic IP Handling: No more hardcoded IPs!
IPAddress targetIP; 
const int TARGET_PORT     = 8080; // Port for the CSV Dashboard
const int DEBUG_PORT      = 8081; // Port for the Wireless Serial Monitor

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
#define MESSAGE_BUFFER_SIZE 128

// ==========================================
// 📡 WIRELESS DEBUG LOGGER
// ==========================================
void remoteLog(String message) {
  // Always print to USB Serial just in case it is plugged in
  Serial.println(message); 
  
  // Fire the exact same message over Wi-Fi to the laptop's debug port
  if (WiFi.status() == WL_CONNECTED) {
    udp.beginPacket(targetIP, DEBUG_PORT);
    udp.print(message);
    udp.endPacket();
  }
}

// ==========================================
// ⚙️ SETUP (REORDERED FOR FIELD DEPLOYMENT)
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1500); // Give the Serial Monitor time to connect if plugged via USB

  // 1. CONNECT TO WIFI FIRST
  // (We do this first so we can broadcast sensor errors wirelessly)
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Connected!");
  
  // 2. AUTO-ASSIGN THE LAPTOP'S IP ADDRESS
  targetIP = WiFi.gatewayIP();
  Serial.print("Target Laptop IP auto-assigned to: ");
  Serial.println(targetIP);

  // Announce successful boot to the wireless monitor
  remoteLog("ESP32 Booting... Network Link Established.");

  // 3. INITIALIZE QUEUE
  sensorDataQueue = xQueueCreate(20, MESSAGE_BUFFER_SIZE);

  // 4. INITIALIZE SENSORS
  Wire.begin();
  Wire.setClock(100000);

  // --- Initialize MPU-6050 ---
  if (!mpu.begin()) {
    remoteLog("CRITICAL ERROR: Failed to find MPU6050");
    while (1) vTaskDelay(10);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ); 
  remoteLog("MPU-6050 Initialized.");

  // --- Initialize MAX30102 ---
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    remoteLog("CRITICAL ERROR: MAX30102 not found");
    while (1) vTaskDelay(10);
  }
  particleSensor.setup(); 
  particleSensor.setPulseAmplitudeRed(0x0A); 
  particleSensor.setPulseAmplitudeGreen(0); 
  remoteLog("MAX30102 Initialized.");

  // --- Initialize DS18B20 ---
  ds18b20.begin();
  ds18b20.setWaitForConversion(false); 
  remoteLog("DS18B20 Core Temp Initialized.");

  // --- Initialize DHT22 ---
  dht.begin();
  remoteLog("DHT22 Ambient Temp Initialized.");

  // 5. START TASKS
  remoteLog("Sensors online. Starting FreeRTOS Tasks...");
  
  xTaskCreatePinnedToCore(TaskSensors, "SensorTask", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(TaskDataSender, "SenderTask", 4096, NULL, 1, NULL, 1);
}

void loop() {
  // FreeRTOS handles everything; loop remains empty.
  vTaskDelay(portMAX_DELAY);
}

// ==========================================
// 🛠️ TASK 1: READ SENSORS (CORE 0)
// ==========================================
void TaskSensors(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(20); // 50Hz Sampling

  for (;;) {
    // 1. Read MPU-6050
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    // 2. Read MAX30102 (IR/Red values)
    long irValue = particleSensor.getIR();
    int heartRate = 0;
    int spo2 = 98; // Simulated SpO2 

    // 3. Read Piezo
    int piezoVal = analogRead(PIEZO_PIN);

    // 4. Read DS18B20
    ds18b20.requestTemperatures();
    float animalTemp = ds18b20.getTempCByIndex(0);

    // 5. Read DHT22
    float envTemp = dht.readTemperature();
    float envHumid = dht.readHumidity();

    // Format Data String
    char dataMessage[MESSAGE_BUFFER_SIZE];
    snprintf(dataMessage, sizeof(dataMessage), 
             "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%.2f,%.2f,%.2f",
             a.acceleration.x, a.acceleration.y, a.acceleration.z,
             g.gyro.x, g.gyro.y, g.gyro.z,
             spo2, heartRate, piezoVal,
             animalTemp, envTemp, envHumid);

    // Send to Queue
    xQueueSend(sensorDataQueue, &dataMessage, 0);

    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

// ==========================================
// 🛠️ TASK 2: SEND UDP DATA (CORE 1)
// ==========================================
void TaskDataSender(void *pvParameters) {
  char dataMessage[MESSAGE_BUFFER_SIZE];

  for (;;) {
    if (xQueueReceive(sensorDataQueue, &dataMessage, portMAX_DELAY) == pdPASS) {
      
      // Sending to dynamically assigned targetIP
      udp.beginPacket(targetIP, TARGET_PORT);
      udp.print(dataMessage);
      udp.endPacket();
      
    }
  }
}