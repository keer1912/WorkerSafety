// WORKER SAFETY SYSTEM - WORKER B (Emergency Responder)
// This device receives emergency alerts from Worker A

#include <M5StickCPlus.h>
#include <esp_now.h>
#include <WiFi.h>
#include <Preferences.h>

// REPLACE WITH THE MAC Address of Worker A's device
uint8_t workerAMacAddress[] = {0x0C, 0x8B, 0x95, 0xA8, 0x15, 0x78};  // Replace with actual MAC

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
bool shouldSendUpdate = false;

// Emergency state tracking
bool emergencyActive = false;
unsigned long emergencyStartTime = 0;
const unsigned long emergencyDisplayTime = 60000; // Show emergency for 1 minute

// Peer info
esp_now_peer_info_t peerInfo;

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
  
  // Check if this is an emergency notification
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
  
  // Only process regular messages if not in emergency state
  if (!emergencyActive) {
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

void setup() {
  // Initialize the M5StickC Plus
  M5.begin();
  M5.Lcd.setRotation(3); // Landscape mode
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Worker Safety System");
  M5.Lcd.println("Worker B (Emergency Responder)");
  
  // Initialize random number generator
  randomSeed(analogRead(0));
  
  // Display MAC Address
  WiFi.mode(WIFI_STA);
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
  
  // Show instructions
  M5.Lcd.setCursor(0, 130);
  M5.Lcd.println("Btn A: Acknowledge emergency");
  M5.Lcd.println("Btn B: Send status update");
}

void loop() {
  M5.update(); // Update button state
  
  // Check emergency state
  checkEmergencyState();
  
  // Only perform normal operations if not in emergency state
  if (!emergencyActive) {
    // Send periodic status update (every 5 seconds)
    if (millis() - lastSentTime > 5000) {
      outgoingData.messageType = 0;
      outgoingData.counter++;
      sprintf(outgoingData.message, "Status: OK, Count: %d", outgoingData.counter);
      outgoingData.keyIndex = keyMgr.keyIndex;
      
      M5.Lcd.fillRect(0, 40, 240, 40, BLACK);
      M5.Lcd.setCursor(0, 40);
      M5.Lcd.setTextColor(CYAN, BLACK);
      M5.Lcd.println("SENDING STATUS UPDATE:");
      M5.Lcd.setTextColor(WHITE, BLACK);
      M5.Lcd.printf("Count: %d\n", outgoingData.counter);
      
      esp_now_send(workerAMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
      lastSentTime = millis();
    }
    
    // Manual status update button (Button B)
    if (M5.BtnB.wasPressed()) {
      outgoingData.messageType = 0;
      outgoingData.counter++;
      sprintf(outgoingData.message, "Manual update: OK");
      outgoingData.keyIndex = keyMgr.keyIndex;
      
      M5.Lcd.fillRect(0, 40, 240, 40, BLACK);
      M5.Lcd.setCursor(0, 40);
      M5.Lcd.setTextColor(CYAN, BLACK);
      M5.Lcd.println("SENDING MANUAL UPDATE:");
      M5.Lcd.setTextColor(WHITE, BLACK);
      M5.Lcd.println("Manual status update");
      
      esp_now_send(workerAMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
      lastSentTime = millis();
    }
  }
  
  delay(50); // Small delay to prevent hogging the CPU
} 