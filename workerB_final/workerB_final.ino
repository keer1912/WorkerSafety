// WORKER SAFETY SYSTEM - WORKER B (Emergency Responder with Fall Detection)
// This device receives emergency alerts from Worker A and has its own fall detection

#include <M5StickCPlus.h>
#include <esp_now.h>
#include <WiFi.h>
#include <Preferences.h>
#include <Wire.h>
#include "MAX30100_PulseOximeter.h"  // Added heart rate sensor library
#include <PubSubClient.h>

//=============== CONFIGURATION SECTION - EDIT THESE VALUES ===============//

// WiFi credentials - update with your network details
const char* ssid = "Xiaomi Nya";          // Your Wi-Fi name
const char* password = "itamenekos";      // Your Wi-Fi password

// MQTT Configuration
const char* mqtt_server = "192.168.60.52";  // Replace with your MQTT broker IP
const int mqtt_port = 1883;                 // Standard MQTT port
const char* mqtt_user = "iot_proj";         // Update with your MQTT username
const char* mqtt_password = "1234";         // Update with your MQTT password

// Worker Identification for MQTT Dashboard
const int floor_id = 1;      // Floor number
const int worker_id = 2;     // Worker number (this is Worker B, so different ID)

// Heart rate sensor pins
#define SDA_PIN 32
#define SCL_PIN 33

//=============== END CONFIGURATION SECTION ===============//

// REPLACE WITH THE MAC Address of Worker A's device
uint8_t workerAMacAddress[] = {0x0C, 0x8B, 0x95, 0xA8, 0x15, 0x78};  // Replace with actual MAC

// WiFi and MQTT clients
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// MQTT topics
String heartrate_topic;
String battery_topic;
String fall_topic;
String status_topic;
String actl_topic;  // Added as per requirement

// Create a preferences object for storing encryption keys
Preferences preferences;

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
unsigned long lastReconnectAttempt = 0;
bool shouldSendUpdate = false;

// Connection status flags
bool wifiConnected = false;
bool mqttConnected = false;

// Emergency state tracking
bool emergencyActive = false;
unsigned long emergencyStartTime = 0;
const unsigned long emergencyDisplayTime = 60000; // Show emergency for 1 minute

// Peer info
esp_now_peer_info_t peerInfo;

// Battery level
int batteryLevel = 100;
unsigned long lastBatteryCheck = 0;
const int batteryCheckInterval = 30000; // Check battery every 30 seconds

// Fall detection variables
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

// Callback function for heart rate sensor
void onBeatDetected() {
  if (userState != FALLEN && !emergencyActive) {
    M5.Lcd.fillRect(0, 100, 240, 10, BLACK);
    M5.Lcd.setCursor(0, 100);
    M5.Lcd.setTextColor(MAGENTA, BLACK);
    M5.Lcd.println("♥️ Beat");
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

// Send emergency fall detection notification (ESP-NOW)
void sendFallEmergency() {
  outgoingData.messageType = 4; // Fall emergency notification
  sprintf(outgoingData.message, "EMERGENCY: WORKER B FALLEN IN FLOOR %d!", floor_id);
  outgoingData.value = accelMagnitude; // Send impact force
  outgoingData.keyIndex = keyMgr.keyIndex;
  outgoingData.heartRate = heartRate;
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
}

// Connect to WiFi network
void connectWiFi() {
  // We need to disconnect from WiFi before changing mode
  WiFi.disconnect();
  delay(100);
  
  // Set WiFi mode to allow both station (for MQTT) and ESP-NOW
  WiFi.mode(WIFI_AP_STA);
  
  // Start WiFi connection
  WiFi.begin(ssid, password);
  
  M5.Lcd.fillRect(0, 40, 240, 20, BLACK);
  M5.Lcd.setCursor(0, 40);
  M5.Lcd.println("Connecting to WiFi...");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    M5.Lcd.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    M5.Lcd.println("\nWiFi connected!");
    M5.Lcd.println(WiFi.localIP().toString());
  } else {
    wifiConnected = false;
    M5.Lcd.println("\nWiFi connection failed");
  }
}

// Connect to MQTT broker
void connectMQTT() {
  if (!wifiConnected) return;
  
  M5.Lcd.fillRect(0, 80, 240, 20, BLACK);
  M5.Lcd.setCursor(0, 80);
  M5.Lcd.println("Connecting to MQTT...");
  
  // Create a unique client ID
  String clientId = "M5StickC_";
  clientId += String(floor_id) + "_" + String(worker_id);
  clientId += "_";
  clientId += String(millis() % 1000);
  
  // Connect to MQTT broker
  if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
    mqttConnected = true;
    M5.Lcd.println("MQTT connected!");
    
    // Announce online status
    mqttClient.publish(status_topic.c_str(), "online");
  } else {
    mqttConnected = false;
    M5.Lcd.println("MQTT connection failed");
  }
}

// Publish fall status to MQTT dashboard
void publishFallStatus() {
  if (!mqttConnected) return;

  // Publish fall status with the specific "Fallen" message as required
  if (userState == FALLEN) {
    mqttClient.publish(fall_topic.c_str(), "Fallen");
    
    // Also publish to the actl topic for the central system
    String jsonPayload = "{\"status\":\"Fallen\",\"floor\":" + String(floor_id) + 
                         ",\"worker\":" + String(worker_id) + 
                         ",\"heartRate\":" + String(heartRate) + 
                         ",\"battery\":" + String(batteryLevel) + "}";
    mqttClient.publish(actl_topic.c_str(), jsonPayload.c_str());
  } else {
    mqttClient.publish(fall_topic.c_str(), "OK");
  }
}

// Publish all sensor data to MQTT dashboard
void publishSensorData() {
  if (!mqttConnected) return;
  
  // Publish heart rate
  String hr_str = String(heartRate);
  mqttClient.publish(heartrate_topic.c_str(), hr_str.c_str());
  
  // Publish battery level
  String batt_str = String(batteryLevel);
  mqttClient.publish(battery_topic.c_str(), batt_str.c_str());
  
  // Publish fall status
  publishFallStatus();
  
  // Send consolidated data to actl topic as JSON
  String status = "OK";
  if (userState == FALLEN) {
    status = "Fallen";
  } else if (emergencyActive) {
    status = "Responding";
  }
  
  String jsonPayload = "{\"status\":\"" + status + 
                       "\",\"floor\":" + String(floor_id) + 
                       ",\"worker\":" + String(worker_id) + 
                       ",\"heartRate\":" + String(heartRate) + 
                       ",\"battery\":" + String(batteryLevel) + "}";
  mqttClient.publish(actl_topic.c_str(), jsonPayload.c_str());
}

// Fall detection function
void detectFall() {
  // Only check for falls if not already in emergency mode (responding to other worker)
  if (emergencyActive) return;
  
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
        
        // Send emergency message to Worker A via ESP-NOW
        sendFallEmergency();
        
        // Update fall status on MQTT dashboard
        publishFallStatus();
        
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
        M5.Lcd.println("Worker B (Emergency Responder)");
        M5.Lcd.println("RECOVERED!");
        
        // Update fall status on MQTT dashboard
        publishFallStatus();
        
        delay(1000);
      }
      break;
  }
}

// Callback function called when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (userState != FALLEN) {
    M5.Lcd.setCursor(0, 120);
    if (status == ESP_NOW_SEND_SUCCESS) {
      M5.Lcd.setTextColor(GREEN, BLACK);
      M5.Lcd.println("Sent OK    ");
    } else {
      M5.Lcd.setTextColor(RED, BLACK);
      M5.Lcd.println("Send FAILED");
    }
  }
}

// Callback function called when data is received
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  memcpy(&incomingData, data, sizeof(incomingData));
  
  // Check if this is an emergency notification
  if (incomingData.messageType == 4) {
    // Only process if not already fallen ourselves
    if (userState != FALLEN) {
      // Display emergency alert
      M5.Lcd.fillScreen(RED);
      M5.Lcd.setCursor(0, 0);
      M5.Lcd.setTextColor(WHITE, RED);
      M5.Lcd.setTextSize(2);
      //M5.Lcd.println("!! EMERGENCY !!");
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
      
      // Publish emergency response status to MQTT
      if (mqttConnected) {
        mqttClient.publish(status_topic.c_str(), "Responding");
        
        // Send consolidated data to actl topic
        String jsonPayload = "{\"status\":\"Responding\"," 
                           "\"floor\":" + String(floor_id) + "," 
                           "\"worker\":" + String(worker_id) + "," 
                           "\"emergency\":\"Worker " + String(floor_id) + "_" + String(worker_id-1) + " fallen\"," 
                           "\"heartRate\":" + String(heartRate) + "," 
                           "\"battery\":" + String(batteryLevel) + "}";
        mqttClient.publish(actl_topic.c_str(), jsonPayload.c_str());
      }
    } else {
      // We're fallen too, but still acknowledge
      outgoingData.messageType = 2;
      sprintf(outgoingData.message, "I'm fallen too, can't help");
      esp_now_send(workerAMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
    }
    
    return;
  }
  
  // Only process regular messages if not in emergency state and not fallen
  if (!emergencyActive && userState != FALLEN) {
    // Handle regular messages
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
  
  lastReceivedTime = millis();
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
      M5.Lcd.println("Worker B (Emergency Responder)");
      M5.Lcd.println("Emergency acknowledged");
      M5.Lcd.println("Resuming normal operation");
      
      // Update MQTT status to show normal operation
      if (mqttConnected) {
        mqttClient.publish(status_topic.c_str(), "OK");
        
        // Send consolidated data to actl topic
        String jsonPayload = "{\"status\":\"OK\"," 
                           "\"floor\":" + String(floor_id) + "," 
                           "\"worker\":" + String(worker_id) + "," 
                           "\"heartRate\":" + String(heartRate) + "," 
                           "\"battery\":" + String(batteryLevel) + "}";
        mqttClient.publish(actl_topic.c_str(), jsonPayload.c_str());
      }
      
      delay(2000);
      
      // Send confirmation of assistance to Worker A
      outgoingData.messageType = 2;
      sprintf(outgoingData.message, "Help is on the way");
      esp_now_send(workerAMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
    }
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
  M5.Lcd.println("Worker B (Emergency Responder)");
  
  // Setup MQTT topics according to the required format: /floorid/workerid/...
  fall_topic = "/" + String(floor_id) + "/" + String(worker_id) + "/falldetect";
  heartrate_topic = "/" + String(floor_id) + "/" + String(worker_id) + "/heartrate";
  battery_topic = "/" + String(floor_id) + "/" + String(worker_id) + "/battery";
  status_topic = "/" + String(floor_id) + "/" + String(worker_id) + "/status";
  actl_topic = "actl";  // Special topic for the central system
  
  // Initialize random number generator
  randomSeed(analogRead(0));
  
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
  
  // Connect to WiFi (needed for MQTT)
  connectWiFi();
  
  // Display MAC Address
  M5.Lcd.println("MAC: " + WiFi.macAddress());
  M5.Lcd.println("Use this MAC for Worker A!");
  
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
  
  // Configure MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
  
  // Connect to MQTT broker
  connectMQTT();
  
  // Get initial battery level
  batteryLevel = getBatteryPercentage();
  
  // Show instructions
  M5.Lcd.setCursor(0, 130);
  M5.Lcd.println("Btn A: Acknowledge alert/recover");
  M5.Lcd.println("Btn B: Send status update");
}

void loop() {
  M5.update(); // Update button state
  
  // Update heart rate sensor
  pox.update();
  
  // Check WiFi connection and reconnect if needed
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    if (millis() - lastReconnectAttempt > 10000) {
      lastReconnectAttempt = millis();
      connectWiFi();
    }
  } else {
    wifiConnected = true;
  }
  
  // Reconnect MQTT if lost
  if (wifiConnected && !mqttClient.connected()) {
    mqttConnected = false;
    if (millis() - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = millis();
      connectMQTT();
    }
  } else if (wifiConnected) {
    mqttConnected = true;
    mqttClient.loop();
  }
  
  // Check heart rate at regular intervals
  if (millis() - lastHeartRateCheck > heartRateCheckInterval) {
    lastHeartRateCheck = millis();
    //heartRate = pox.getHeartRate();
    heartRate = random(50, 120);  // Temporary random value for testing
    
    // Update display with heart rate if not in fallen state or emergency state
    if (userState != FALLEN && !emergencyActive) {
      M5.Lcd.fillRect(0, 60, 240, 10, BLACK);
      M5.Lcd.setCursor(0, 60);
      M5.Lcd.printf("HR: %.1f BPM\n", heartRate);
    }
  }
  
  // Check battery level at regular intervals
  if (millis() - lastBatteryCheck > batteryCheckInterval) {
    lastBatteryCheck = millis();
    batteryLevel = getBatteryPercentage();
    
    // Update display with battery level if not in emergency or fallen state
    if (!emergencyActive && userState != FALLEN) {
      M5.Lcd.fillRect(0, 70, 240, 10, BLACK);
      M5.Lcd.setCursor(0, 70);
      M5.Lcd.printf("Batt: %d%%\n", batteryLevel);
    }
  }
  
  // Check for falls (only if not in emergency response mode)
  if (!emergencyActive) {
    detectFall();
  } else {
    // Check if emergency response state needs to be cleared
    checkEmergencyState();
  }
  
  // Only perform normal operations if not in emergency or fallen state
  if (!emergencyActive && userState != FALLEN) {
    // Send periodic status update via ESP-NOW (every 5 seconds)
    if (millis() - lastSentTime > 5000) {
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
    
    // Publish sensor data to MQTT dashboard (every 2 seconds)
    if (mqttConnected && millis() - lastMqttPublishTime > 2000) {
      lastMqttPublishTime = millis();
      publishSensorData();
    }
    
    // Manual status update button (Button B)
    if (M5.BtnB.wasPressed()) {
      outgoingData.messageType = 0;
      outgoingData.counter++;
      sprintf(outgoingData.message, "Manual update: OK");
      outgoingData.keyIndex = keyMgr.keyIndex;
      outgoingData.heartRate = heartRate;
      outgoingData.batteryLevel = getBatteryPercentage();
      
      M5.Lcd.fillRect(0, 40, 240, 40, BLACK);
      M5.Lcd.setCursor(0, 40);
      M5.Lcd.setTextColor(CYAN, BLACK);
      M5.Lcd.println("SENDING MANUAL UPDATE:");
      M5.Lcd.setTextColor(WHITE, BLACK);
      M5.Lcd.println("Manual status update");
      
      esp_now_send(workerAMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
      lastSentTime = millis();
      
      // Also publish to MQTT
      if (mqttConnected) {
        publishSensorData();
      }
    }
  }
  
  delay(20); // Small delay to prevent hogging the CPU
}
