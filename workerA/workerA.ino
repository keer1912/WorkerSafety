// WORKER SAFETY SYSTEM - WORKER A (Fall Detection)
// This device is worn by the worker and detects falls

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

// REPLACE WITH THE MAC Address of Worker B's device
uint8_t workerBMacAddress[] = {0x4C, 0x75, 0x25, 0xCB, 0x90, 0x88};  // Replace with actual MAC

// Create a preferences object for storing encryption keys
Preferences preferences;

// Define a structure for key management
typedef struct {
  uint8_t currentKey[16];
  uint32_t keyRotationTime;
  uint16_t keyIndex;
} KeyManager;

KeyManager keyMgr;

// Define a data structure for communication
typedef struct message_struct {
  uint8_t messageType;  // 0 = regular update, 1 = button press, 2 = response, 3 = key update, 4 = fall emergency
  char message[32];
  int counter;
  float value;
  uint16_t keyIndex;
  float heartRate;    // Added heart rate field
  int batteryLevel;   // Added battery level field
} message_struct;

// Create structured objects
message_struct outgoingData;
message_struct incomingData;

// Status tracking
unsigned long lastSentTime = 0;
unsigned long lastReceivedTime = 0;
bool shouldSendUpdate = false;

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
  if (userState != FALLEN) {
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

// Send emergency fall detection notification
void sendFallEmergency() {
  outgoingData.messageType = 4; // Fall emergency notification
  sprintf(outgoingData.message, "EMERGENCY: WORKER FALLEN!");
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
  esp_now_send(workerBMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
  
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
  
  // Handle emergency response
  if (incomingData.messageType == 2 && userState == FALLEN) {
    M5.Lcd.fillRect(0, 80, 240, 40, BLACK);
    M5.Lcd.setCursor(0, 80);
    M5.Lcd.setTextColor(GREEN, BLACK);
    M5.Lcd.println("Help is coming!");
    M5.Lcd.println(incomingData.message);
    
    return;
  }
  
  // Handle regular messages
  if (incomingData.messageType == 0) {
    M5.Lcd.fillRect(0, 80, 240, 40, BLACK);
    M5.Lcd.setCursor(0, 80);
    M5.Lcd.setTextColor(YELLOW, BLACK);
    M5.Lcd.println("Received from Worker B:");
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.printf("Status: %s\n", incomingData.message);
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
        
        // Send emergency message to Worker B
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
        M5.Lcd.println("RECOVERED!");
        
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
  M5.Lcd.println("Worker A (Fall Detection)");
  
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
  
  // Initialize WiFi in AP+STA mode
  WiFi.mode(WIFI_STA);
  M5.Lcd.println("MAC: " + WiFi.macAddress());
  M5.Lcd.println("Use this MAC for Worker B!");
  
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
  memcpy(peerInfo.peer_addr, workerBMacAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = true;
  
  // Add peer        
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    M5.Lcd.println("Failed to add peer");
    return;
  }
  
  // Initialize outgoing data
  outgoingData.messageType = 0;
  strcpy(outgoingData.message, "Worker A Online");
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
  M5.Lcd.println("Btn A: Recover from fall");
  M5.Lcd.println("Btn B: Manual alert");
}

void loop() {
  M5.update(); // Update button state
  
  // Update heart rate sensor
  pox.update();
  
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
      
      esp_now_send(workerBMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
      lastSentTime = millis();
    }
  }
  
  // Manual alert button (Button B)
  if (M5.BtnB.wasPressed() && userState != FALLEN) {
    outgoingData.messageType = 4; // Emergency notification
    sprintf(outgoingData.message, "MANUAL ALERT: HELP NEEDED!");
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
    
    esp_now_send(workerBMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
    
    lastSentTime = millis();
  }
  
  delay(20); // Small delay to prevent hogging the CPU
} 