// WORKER SAFETY SYSTEM - WORKER B
// This device has both fall detection and emergency response capabilities

#include <M5StickCPlus.h>
#include <esp_now.h>
#include <WiFi.h>
#include <Preferences.h>
#include <Wire.h>
#include "MAX30100_PulseOximeter.h"
#include <PubSubClient.h>

// Heart rate sensor pins
#define SDA_PIN 32  
#define SCL_PIN 33

// WiFi and MQTT settings
const char* ssid = "your-wifi-ssid";     // Replace with your WiFi SSID
const char* password = "your-wifi-pass"; // Replace with your WiFi password
const char* mqtt_server = "192.168.x.x"; // Replace with your MQTT broker IP
const int mqtt_port = 1883;
const char* mqtt_user = "motherpi";      // MQTT username if needed
const char* mqtt_password = "12345678";  // MQTT password if needed

// Worker and floor information
const char* workerID = "worker2";        // Change per device
const char* floorID = "floor1";          // Change per location

// MQTT topics
char topic_fall[50];
char topic_heartrate[50];
char topic_battery[50];
char topic_recovery[50];
char clientId[50];

// REPLACE WITH THE MAC Address of Worker A's device
uint8_t workerAMacAddress[] = {0x0C, 0x8B, 0x95, 0xA8, 0x15, 0x78};  // Replace with actual MAC

// Create a preferences object for storing encryption keys
Preferences preferences;

// WiFi and MQTT clients
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Define a structure for key management
typedef struct {
  uint8_t currentKey[16];
  uint32_t keyRotationTime;
  uint16_t keyIndex;
} KeyManager;

KeyManager keyMgr;

// Define the same data structure as in Worker A
typedef struct message_struct {
  uint8_t messageType;  // 0 = regular update, 1 = button press, 2 = response, 3 = key update, 4 = fall emergency
  char message[32];
  int counter;
  float value;
  uint16_t keyIndex;
  float heartRate;     // Added heart rate field
  int batteryLevel;    // Added battery level field
} message_struct;

// Create structured objects
message_struct outgoingData;
message_struct incomingData;

// Status tracking
unsigned long lastSentTime = 0;
unsigned long lastReceivedTime = 0;
unsigned long lastMqttPublishTime = 0;
unsigned long lastMqttReconnectAttempt = 0;
bool shouldSendUpdate = false;
bool wifiConnected = false;
bool mqttConnected = false;

// Emergency state tracking (for receiving alerts)
bool emergencyActive = false;
unsigned long emergencyStartTime = 0;
const unsigned long emergencyDisplayTime = 60000; // Show emergency for 1 minute

// Peer info
esp_now_peer_info_t peerInfo;

// Accelerometer data for fall detection
float accX, accY, accZ;
float accelMagnitude;
float impactThreshold = 3.0; // Threshold for detecting a fall

// State machine for fall detection
enum UserState {STANDING, FALLEN};
UserState userState = STANDING;

bool possibleFall = false;
unsigned long fallDetectionTime = 0;
const int debounceTime = 2000;  // Prevent multiple detections

// Moving average for acceleration smoothing
const int movingAverageWindow = 10;
float accelMagnitudeHistory[movingAverageWindow];
int accelMagnitudeIndex = 0;
float accelMagnitudeAverage = 0;

// Heart rate sensor
PulseOximeter pox;
float heartRate = 0.0;
unsigned long lastHeartRateCheck = 0;
const int heartRateCheckInterval = 1000; // Check heart rate every second

// Battery level
int batteryLevel = 100;
unsigned long lastBatteryCheck = 0;
const int batteryCheckInterval = 30000; // Check battery every 30 seconds

// Callback function for heart rate sensor
void onBeatDetected() {
  if (userState != FALLEN && !emergencyActive) {
    M5.Lcd.fillRect(0, 100, 240, 10, BLACK);
    M5.Lcd.setCursor(0, 100);
    M5.Lcd.setTextColor(MAGENTA, BLACK);
    M5.Lcd.println("♥ Beat");
  }
}

// Basic key generation function
void generateNewKey(uint8_t* keyBuffer) {
  for (int i = 0; i < 16; i++) {
    keyBuffer[i] = random(0, 256);
  }
}

// Initialize the key manager
void initializeKeyManager() {
  preferences.begin("worker-safety-keys", false);
  
  keyMgr.keyIndex = preferences.getUShort("keyIndex", 0);
  keyMgr.keyRotationTime = preferences.getULong("nextRotation", 0);
  
  if (keyMgr.keyIndex == 0 || keyMgr.keyRotationTime == 0 || millis() > keyMgr.keyRotationTime) {
    keyMgr.keyIndex++;
    generateNewKey(keyMgr.currentKey);
    preferences.putBytes("currentKey", keyMgr.currentKey, 16);
    preferences.putUShort("keyIndex", keyMgr.keyIndex);
    keyMgr.keyRotationTime = millis() + (7 * 24 * 60 * 60 * 1000); // 7 days
    preferences.putULong("nextRotation", keyMgr.keyRotationTime);
  } else {
    preferences.getBytes("currentKey", keyMgr.currentKey, 16);
  }

  M5.Lcd.printf("Key Index: %d\n", keyMgr.keyIndex);
}

// Apply the current key to ESP-NOW
void applyCurrentKey() {
  esp_err_t result = esp_now_set_pmk(keyMgr.currentKey);
  if (result != ESP_OK) {
    M5.Lcd.println("Failed to set encryption key");
  } else {
    M5.Lcd.println("Encryption enabled");
  }
}

// Get battery percentage
int getBatteryPercentage() {
  float batVoltage = M5.Axp.GetVbatData() * 1.1 / 1000;
  int percentage = (batVoltage - 3.0) / (4.2 - 3.0) * 100;
  return constrain(percentage, 0, 100); // Clamp to 0-100%
}

// Connect to WiFi
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    return;
  }
  
  M5.Lcd.fillRect(0, 110, 240, 20, BLACK);
  M5.Lcd.setCursor(0, 110);
  M5.Lcd.println("Connecting to WiFi...");
  
  // Start connection while maintaining ESP-NOW capability
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    delay(500);
    M5.Lcd.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    M5.Lcd.fillRect(0, 110, 240, 20, BLACK);
    M5.Lcd.setCursor(0, 110);
    M5.Lcd.print("WiFi connected: ");
    M5.Lcd.println(WiFi.localIP());
  } else {
    wifiConnected = false;
    M5.Lcd.fillRect(0, 110, 240, 20, BLACK);
    M5.Lcd.setCursor(0, 110);
    M5.Lcd.println("WiFi connection failed");
  }
}

// Reconnect to MQTT broker
void reconnectMQTT() {
  if (!wifiConnected) {
    connectWiFi(); // Ensure WiFi is connected first
    if (!wifiConnected) return;
  }
  
  if (mqttClient.connected()) {
    mqttConnected = true;
    return;
  }
  
  M5.Lcd.fillRect(0, 110, 240, 20, BLACK);
  M5.Lcd.setCursor(0, 110);
  M5.Lcd.println("Connecting to MQTT...");
  
  // Create a client ID
  sprintf(clientId, "M5Stick_%s_%s", floorID, workerID);
  
  // Attempt to connect
  if (mqttClient.connect(clientId, mqtt_user, mqtt_password)) {
    mqttConnected = true;
    M5.Lcd.fillRect(0, 110, 240, 20, BLACK);
    M5.Lcd.setCursor(0, 110);
    M5.Lcd.println("MQTT connected");
  } else {
    mqttConnected = false;
    M5.Lcd.fillRect(0, 110, 240, 20, BLACK);
    M5.Lcd.setCursor(0, 110);
    M5.Lcd.print("MQTT failed, rc=");
    M5.Lcd.println(mqttClient.state());
  }
}

// MQTT callback
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Handle incoming MQTT messages if needed
}

// Publish data to MQTT
void publishDataToMQTT() {
  if (!mqttConnected) return;
  
  // Publish heart rate
  char heartRateMsg[10];
  sprintf(heartRateMsg, "%.1f", heartRate);
  mqttClient.publish(topic_heartrate, heartRateMsg);
  
  // Publish battery level
  char batteryMsg[10];
  sprintf(batteryMsg, "%d", batteryLevel);
  mqttClient.publish(topic_battery, batteryMsg);
}

// Send emergency fall detection notification
void sendFallEmergency() {
  outgoingData.messageType = 4; // Fall emergency notification
  sprintf(outgoingData.message, "EMERGENCY: WORKER B FALLEN!");
  outgoingData.value = accelMagnitude; // Send impact force
  outgoingData.keyIndex = keyMgr.keyIndex;
  outgoingData.heartRate = heartRate; // Include heart rate in emergency message
  outgoingData.batteryLevel = batteryLevel;
  
  // Display emergency message
  M5.Lcd.fillRect(0, 40, 240, 40, BLACK);
  M5.Lcd.setCursor(0, 40);
  M5.Lcd.setTextColor(RED, BLACK);
  M5.Lcd.println("SENDING EMERGENCY ALERT:");
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.println("Fall detected, help needed!");
  
  // Send emergency message via ESP-NOW
  esp_now_send(workerAMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
  
  // Send emergency message via MQTT if connected
  if (mqttConnected) {
    mqttClient.publish(topic_fall, "Worker B Fall Detected!");
  }
}

// Check if emergency state needs to be cleared
void checkEmergencyState() {
  if (emergencyActive) {
    // If Button A is pressed or emergency display time has elapsed
    if (M5.BtnA.wasPressed() || (millis() - emergencyStartTime > emergencyDisplayTime)) {
      emergencyActive = false;
      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setTextSize(1);
      M5.Lcd.setCursor(0, 0);
      M5.Lcd.println("Worker Safety System");
      M5.Lcd.println("Worker B");
      M5.Lcd.println("Emergency acknowledged");
      M5.Lcd.println("Resuming normal operation");
      delay(2000);
      
      // Send confirmation of assistance to Worker A
      outgoingData.messageType = 2;
      sprintf(outgoingData.message, "Help is on the way");
      esp_now_send(workerAMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
    }
  }
}

// Callback function called when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  M5.Lcd.setCursor(0, 120);
  if (status == ESP_NOW_SEND_SUCCESS) {
    M5.Lcd.setTextColor(GREEN, BLACK);
    M5.Lcd.println("Sent OK    ");
  } else {
    M5.Lcd.setTextColor(RED, BLACK);
    M5.Lcd.println("Send FAILED");
  }
}

// Callback function called when data is received
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  memcpy(&incomingData, data, sizeof(incomingData));
  
  // Check if this is an emergency notification from Worker A
  if (incomingData.messageType == 4) {
    // Display emergency alert
    M5.Lcd.fillScreen(RED);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.setTextColor(WHITE, RED);
    M5.Lcd.setTextSize(2);
    M5.Lcd.println("!! EMERGENCY !!");
    M5.Lcd.println(incomingData.message);
    M5.Lcd.println("WORKER A NEEDS HELP");
    M5.Lcd.setTextSize(1);
    
    // Display vital information
    if (incomingData.value > 0) {
      M5.Lcd.printf("Impact force: %.2f\n", incomingData.value);
    }
    M5.Lcd.printf("Heart Rate: %.1f BPM\n", incomingData.heartRate);
    M5.Lcd.printf("Battery: %d%%\n", incomingData.batteryLevel);
    
    // Play alarm sound
    for (int i = 0; i < 5; i++) {
      M5.Beep.beep();
      delay(300);
      M5.Beep.mute();
      delay(100);
    }
    
    // Set emergency state
    emergencyActive = true;
    emergencyStartTime = millis();
    
    // Send acknowledgment
    outgoingData.messageType = 2;
    sprintf(outgoingData.message, "Emergency received, help coming");
    esp_now_send(workerAMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
    return;
  }
  
  // Handle emergency response
  if (incomingData.messageType == 2 && userState == FALLEN) {
    M5.Lcd.fillRect(0, 80, 240, 40, BLACK);
    M5.Lcd.setCursor(0, 80);
    M5.Lcd.setTextColor(GREEN, BLACK);
    M5.Lcd.println("Help is coming!");
    M5.Lcd.println(incomingData.message);
    
    // If we're recovered from a fall, publish to MQTT
    if (mqttConnected) {
      mqttClient.publish(topic_recovery, "Help is on the way");
    }
    return;
  }
  
  // Only process regular messages if not in emergency state
  if (!emergencyActive && userState != FALLEN) {
    // Handle regular messages
    if (incomingData.messageType == 0) {
      M5.Lcd.fillRect(0, 80, 240, 60, BLACK);
      M5.Lcd.setCursor(0, 80);
      M5.Lcd.setTextColor(YELLOW, BLACK);
      M5.Lcd.println("Received from Worker A:");
      M5.Lcd.setTextColor(WHITE, BLACK);
      M5.Lcd.printf("Status: %s\n", incomingData.message);
      
      // Display vital information
      if (incomingData.heartRate > 0) {
        M5.Lcd.printf("Heart Rate: %.1f BPM\n", incomingData.heartRate);
        
        // Alert if heart rate is abnormal
        if (incomingData.heartRate > 100) {
          M5.Lcd.setTextColor(RED, BLACK);
          M5.Lcd.println("WARNING: High Heart Rate!");
          M5.Lcd.setTextColor(WHITE, BLACK);
        } else if (incomingData.heartRate < 50) {
          M5.Lcd.setTextColor(RED, BLACK);
          M5.Lcd.println("WARNING: Low Heart Rate!");
          M5.Lcd.setTextColor(WHITE, BLACK);
        }
      }
      
      M5.Lcd.printf("Battery: %d%%\n", incomingData.batteryLevel);
      if (incomingData.batteryLevel < 20) {
        M5.Lcd.setTextColor(RED, BLACK);
        M5.Lcd.println("WARNING: Low Battery!");
        M5.Lcd.setTextColor(WHITE, BLACK);
      }
    }
  }
  
  lastReceivedTime = millis();
}

// Fall detection function
void detectFall() {
  // Read accelerometer data
  M5.IMU.getAccelData(&accX, &accY, &accZ);
  accelMagnitude = sqrt(accX*accX + accY*accY + accZ*accZ);
  
  // Update moving average
  accelMagnitudeHistory[accelMagnitudeIndex] = accelMagnitude;
  accelMagnitudeIndex = (accelMagnitudeIndex + 1) % movingAverageWindow;
  accelMagnitudeAverage = 0;
  for (int i = 0; i < movingAverageWindow; i++) {
    accelMagnitudeAverage += accelMagnitudeHistory[i];
  }
  accelMagnitudeAverage /= movingAverageWindow;
  
  // Fall detection logic
  switch (userState) {
    case STANDING:
      if (accelMagnitude > impactThreshold) {
        userState = FALLEN;
        M5.Lcd.fillScreen(RED);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.setTextColor(WHITE, RED);
        M5.Lcd.setTextSize(2);
        M5.Lcd.println("FALL DETECTED!");
        M5.Lcd.setTextSize(1);
        M5.Lcd.printf("Impact: %.2f\n", accelMagnitude);
        M5.Lcd.printf("HR: %.1f BPM\n", heartRate);
        
        M5.Beep.beep();  // Alert sound
        delay(1000);
        M5.Beep.mute();
        
        // Send emergency message to Worker A
        sendFallEmergency();
        
        delay(debounceTime);
      }
      break;

    case FALLEN:
      // Manual recovery button (press A to stand up)
      if (M5.BtnA.wasPressed()) {
        userState = STANDING;
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.println("Worker Safety System");
        M5.Lcd.println("Worker B");
        M5.Lcd.println("RECOVERED!");
        
        // Publish recovery message to MQTT if connected
        if (mqttConnected) {
          mqttClient.publish(topic_recovery, "Worker B has recovered from fall");
        }
        
        delay(1000);
      }
      break;
  }
}

void setup() {
  // Initialize the M5StickC Plus
  M5.begin();
  M5.IMU.Init();  // Initialize IMU for fall detection
  M5.Lcd.setRotation(3); // Landscape mode
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Worker Safety System");
  M5.Lcd.println("Worker B");
  
  // Initialize random number generator
  randomSeed(analogRead(0));
  
  // Generate MQTT topics
  sprintf(topic_fall, "/%s/%s/falldetect", floorID, workerID);
  sprintf(topic_heartrate, "/%s/%s/heartrate", floorID, workerID);
  sprintf(topic_battery, "/%s/%s/battery", floorID, workerID);
  sprintf(topic_recovery, "/%s/%s/recovery", floorID, workerID);
  
  // Initialize I2C for heart rate sensor
  Wire.begin(SDA_PIN, SCL_PIN);
  
  // Initialize heart rate sensor
  M5.Lcd.println("Initializing heart rate sensor...");
  if (!pox.begin()) {
    M5.Lcd.println("Heart rate sensor FAILED");
    delay(1000);
  } else {
    M5.Lcd.println("Heart rate sensor ready");
    pox.setOnBeatDetectedCallback(onBeatDetected);
  }
  
  // Initialize WiFi in AP+STA mode
  WiFi.mode(WIFI_AP_STA);
  M5.Lcd.println("MAC: " + WiFi.macAddress());
  M5.Lcd.println("Use this MAC for Worker A!");
  
  // Connect to WiFi
  connectWiFi();
  
  // Setup MQTT connection
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  
  // Try to connect to MQTT broker
  if (wifiConnected) {
    reconnectMQTT();
  }
  
  // Initialize the key manager
  initializeKeyManager();
  
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    M5.Lcd.println("Error initializing ESP-NOW");
    return;
  }
  
  // Apply the current encryption key
  applyCurrentKey();
  
  // Register callbacks
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  // Register peer
  memcpy(peerInfo.peer_addr, workerAMacAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = true;
  
  // Add peer        
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    M5.Lcd.println("Failed to add peer");
    return;
  }
  
  // Initialize outgoing data
  outgoingData.messageType = 0;
  strcpy(outgoingData.message, "Worker B Online");
  outgoingData.counter = 0;
  outgoingData.value = 0.0;
  outgoingData.keyIndex = keyMgr.keyIndex;
  outgoingData.heartRate = 0.0;
  outgoingData.batteryLevel = 100;
  
  // Initialize moving average for fall detection
  for (int i = 0; i < movingAverageWindow; i++) {
    accelMagnitudeHistory[i] = 1.0; // Start with gravity (1G)
  }
  
  // Get initial battery level
  batteryLevel = getBatteryPercentage();
  
  // Show instructions
  M5.Lcd.setCursor(0, 130);
  M5.Lcd.println("Btn A: Recover/Acknowledge");
  M5.Lcd.println("Btn B: Manual alert");
}

void loop() {
  M5.update(); // Update button state
  
  // Update heart rate sensor
  pox.update();
  
  // Check if in emergency response mode
  if (emergencyActive) {
    checkEmergencyState();
    return; // Skip normal processing while in emergency response mode
  }
  
  // Check heart rate at regular intervals
  if (millis() - lastHeartRateCheck > heartRateCheckInterval) {
    lastHeartRateCheck = millis();
    heartRate = pox.getHeartRate();
    
    // Update display with heart rate if not in fallen state
    if (userState != FALLEN) {
      M5.Lcd.fillRect(0, 60, 240, 10, BLACK);
      M5.Lcd.setCursor(0, 60);
      M5.Lcd.printf("HR: %.1f BPM\n", heartRate);
    }
  }
  
  // Check battery level at regular intervals
  if (millis() - lastBatteryCheck > batteryCheckInterval) {
    lastBatteryCheck = millis();
    batteryLevel = getBatteryPercentage();
    
    // Update display with battery level if not in fallen state
    if (userState != FALLEN) {
      M5.Lcd.fillRect(0, 70, 240, 10, BLACK);
      M5.Lcd.setCursor(0, 70);
      M5.Lcd.printf("Batt: %d%%\n", batteryLevel);
    }
  }
  
  // Check WiFi and MQTT connectivity
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    mqttConnected = false;
    
    // Try to reconnect WiFi every 30 seconds
    if (millis() - lastMqttReconnectAttempt > 30000) {
      lastMqttReconnectAttempt = millis();
      connectWiFi();
    }
  } else {
    wifiConnected = true;
    
    // Check MQTT connection
    if (!mqttClient.connected()) {
      mqttConnected = false;
      
      // Try to reconnect MQTT every 5 seconds
      if (millis() - lastMqttReconnectAttempt > 5000) {
        lastMqttReconnectAttempt = millis();
        reconnectMQTT();
      }
    } else {
      mqttConnected = true;
      mqttClient.loop();
    }
  }
  
  // Check for falls
  detectFall();
  
  // Send periodic status update (every 5 seconds)
  if (millis() - lastSentTime > 5000) {
    // Only send if not in FALLEN state
    if (userState != FALLEN) {
      outgoingData.messageType = 0;
      outgoingData.counter++;
      sprintf(outgoingData.message, "Status: OK, Count: %d", outgoingData.counter);
      outgoingData.keyIndex = keyMgr.keyIndex;
      outgoingData.heartRate = heartRate;
      outgoingData.batteryLevel = batteryLevel;
      
      M5.Lcd.fillRect(0, 40, 240, 40, BLACK);
      M5.Lcd.setCursor(0, 40);
      M5.Lcd.setTextColor(CYAN, BLACK);
      M5.Lcd.println("SENDING STATUS UPDATE:");
      M5.Lcd.setTextColor(WHITE, BLACK);
      M5.Lcd.printf("Count: %d, HR: %.1f\n", outgoingData.counter, heartRate);
      
      esp_now_send(workerAMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
      lastSentTime = millis();
    }
  }
  
  // Publish data to MQTT every 10 seconds
  if (mqttConnected && millis() - lastMqttPublishTime > 10000) {
    lastMqttPublishTime = millis();
    publishDataToMQTT();
  }
  
  // Manual alert button (Button B)
  if (M5.BtnB.wasPressed() && userState != FALLEN) {
    outgoingData.messageType = 4; // Emergency notification
    sprintf(outgoingData.message, "MANUAL ALERT: WORKER B NEEDS HELP!");
    outgoingData.value = 0.0;
    outgoingData.keyIndex = keyMgr.keyIndex;
    outgoingData.heartRate = heartRate;
    outgoingData.batteryLevel = batteryLevel;
    
    M5.Lcd.fillRect(0, 40, 240, 40, BLACK);
    M5.Lcd.setCursor(0, 40);
    M5.Lcd.setTextColor(RED, BLACK);
    M5.Lcd.println("SENDING MANUAL ALERT:");
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.println("Manual emergency alert!");
    
    esp_now_send(workerAMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
    
    // Also send to MQTT if connected
    if (mqttConnected) {
      mqttClient.publish(topic_fall, "Worker B Manual Emergency Alert!");
    }
    
    lastSentTime = millis();
  }
  
  delay(20); // Small delay to prevent hogging the CPU
} 