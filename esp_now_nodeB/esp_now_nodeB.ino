// BIDIRECTIONAL ESP-NOW COMMUNICATION WITH FALL DETECTION - DEVICE B

#include <M5StickCPlus.h>
#include <esp_now.h>
#include <WiFi.h>
#include <Preferences.h>  // For storing keys in non-volatile memory

// REPLACE WITH THE MAC Address of your first device (Device A)
uint8_t peerMacAddress[] = {0x0C, 0x8B, 0x95, 0xA8, 0x15, 0x78};  // Replace with actual MAC

// Create a preferences object for storing encryption keys
Preferences preferences;

// Define a structure for key management
typedef struct {
  uint8_t currentKey[16];
  uint32_t keyRotationTime;  // Unix timestamp for next rotation
  uint16_t keyIndex;         // Current key index/version
} KeyManager;

KeyManager keyMgr;

// Define the same data structure as in Device A
typedef struct message_struct {
  uint8_t messageType;  // 0 = regular update, 1 = button press notification, 2 = response, 3 = key update notification, 4 = fall emergency
  char message[32];
  int counter;
  float value;
  // Add key information for key update messages
  uint16_t keyIndex;
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

// Basic key generation function (in real application, use better randomization)
void generateNewKey(uint8_t* keyBuffer) {
  // Generate a random key
  for (int i = 0; i < 16; i++) {
    keyBuffer[i] = random(0, 256);  // Random byte between 0-255
  }
}

// Initialize the key manager
void initializeKeyManager() {
  preferences.begin("esp-now-keys", false);  // Open in RW mode
  
  // Check if we have a stored key
  keyMgr.keyIndex = preferences.getUShort("keyIndex", 0);
  keyMgr.keyRotationTime = preferences.getULong("nextRotation", 0);
  
  if (keyMgr.keyIndex == 0 || keyMgr.keyRotationTime == 0 || millis() > keyMgr.keyRotationTime) {
    // First time or time to rotate keys
    keyMgr.keyIndex++;
    
    // Generate a new key
    generateNewKey(keyMgr.currentKey);
    
    // Save the new key
    preferences.putBytes("currentKey", keyMgr.currentKey, 16);
    preferences.putUShort("keyIndex", keyMgr.keyIndex);
    
    // Set next rotation time (e.g., 7 days from now)
    // In a real application, you would use proper time libraries and NTP
    keyMgr.keyRotationTime = millis() + (7 * 24 * 60 * 60 * 1000); // 7 days in ms
    preferences.putULong("nextRotation", keyMgr.keyRotationTime);
  } else {
    // Load the existing key
    preferences.getBytes("currentKey", keyMgr.currentKey, 16);
  }

  // Debug info
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

// Check if it's time to rotate keys
void checkKeyRotation() {
  if (millis() > keyMgr.keyRotationTime) {
    M5.Lcd.fillRect(0, 40, 240, 40, BLACK); // Clear only send area
    M5.Lcd.setCursor(0, 40);
    M5.Lcd.println("Rotating encryption keys...");
    
    // Generate a new key
    keyMgr.keyIndex++;
    generateNewKey(keyMgr.currentKey);
    
    // Save the new key
    preferences.putBytes("currentKey", keyMgr.currentKey, 16);
    preferences.putUShort("keyIndex", keyMgr.keyIndex);
    
    // Set next rotation time
    keyMgr.keyRotationTime = millis() + (7 * 24 * 60 * 60 * 1000); // 7 days in ms
    preferences.putULong("nextRotation", keyMgr.keyRotationTime);
    
    // Apply the new key
    applyCurrentKey();
    
    // Notify peer about the new key
    sendKeyUpdateNotification();
  }
}

// Send a notification about a key update
void sendKeyUpdateNotification() {
  // Create a key update message
  outgoingData.messageType = 3; // Key update notification
  sprintf(outgoingData.message, "Key update");
  outgoingData.keyIndex = keyMgr.keyIndex;
  
  // Send message via ESP-NOW
  esp_now_send(peerMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
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
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  
  // Copy the data to our variable
  memcpy(&incomingData, data, sizeof(incomingData));
  
  // Check if this is a key update notification
  if (incomingData.messageType == 3) {
    // In a real implementation, you would have a secure way to share the new key
    // For this example, just acknowledge that an update happened
    M5.Lcd.fillRect(0, 80, 240, 40, BLACK); // Clear only receive area
    M5.Lcd.setCursor(0, 80);
    M5.Lcd.setTextColor(MAGENTA, BLACK);
    M5.Lcd.println("RECEIVED KEY UPDATE:");
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.printf("Peer updated to key index: %d\n", incomingData.keyIndex);
    return;
  }
  
  // Check if this is an emergency fall notification
  if (incomingData.messageType == 4) {
    // Display emergency alert
    M5.Lcd.fillScreen(RED);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.setTextColor(WHITE, RED);
    M5.Lcd.setTextSize(2);
    M5.Lcd.println("!! EMERGENCY !!");
    M5.Lcd.println(incomingData.message);
    M5.Lcd.println("WORKER A HAS FALLEN");
    M5.Lcd.setTextSize(1);
    M5.Lcd.printf("Impact force: %.2f\n", incomingData.value);
    
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
    esp_now_send(peerMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
    return;
  }
  
  // Only process regular messages if not in emergency state
  if (!emergencyActive) {
    // Handle regular messages
    M5.Lcd.fillRect(0, 80, 240, 40, BLACK); // Clear only receive area
    M5.Lcd.setCursor(0, 80);
    M5.Lcd.setTextColor(YELLOW, BLACK);
    M5.Lcd.println("RECEIVED FROM DEVICE A:");
    M5.Lcd.setTextColor(WHITE, BLACK);
    
    // Display different info based on message type
    switch(incomingData.messageType) {
      case 0:
        M5.Lcd.printf("Update: %s, Count: %d\n", incomingData.message, incomingData.counter);
        break;
      case 1:
        M5.Lcd.printf("Button pressed on Device A\n");
        M5.Lcd.printf("Counter: %d, Value: %.1f\n", incomingData.counter, incomingData.value);
        
        // Send response
        outgoingData.messageType = 2;
        sprintf(outgoingData.message, "Received your button press");
        esp_now_send(peerMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
        break;
      case 2:
        M5.Lcd.printf("Response: %s\n", incomingData.message);
        break;
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
      
      // Send confirmation of assistance to Device A
      outgoingData.messageType = 2;
      sprintf(outgoingData.message, "Help is on the way");
      esp_now_send(peerMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
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
  M5.Lcd.println("M5StickC Plus ESP-NOW");
  M5.Lcd.println("DEVICE B (Emergency Responder)");
  
  // Initialize random number generator
  randomSeed(analogRead(0));
  
  // Display MAC Address
  WiFi.mode(WIFI_STA);
  M5.Lcd.println("MAC: " + WiFi.macAddress());
  M5.Lcd.println("Use this MAC for Device A!");
  
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
  memcpy(peerInfo.peer_addr, peerMacAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = true;  // Enable encryption for this peer
  
  // Add peer        
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    M5.Lcd.println("Failed to add peer");
    return;
  }
  
  // Initialize outgoing data
  outgoingData.messageType = 0;
  strcpy(outgoingData.message, "Hello from Device B");
  outgoingData.counter = 0;
  outgoingData.value = 25.0;
  outgoingData.keyIndex = keyMgr.keyIndex;
  
  // Show instructions
  M5.Lcd.setCursor(0, 130);
  M5.Lcd.println("Btn A: Send update/Acknowledge");
  M5.Lcd.println("Btn B: Notify button press");
}

void loop() {
  M5.update(); // Update button state
  
  // Check emergency state
  checkEmergencyState();
  
  // Only perform normal operations if not in emergency state
  if (!emergencyActive) {
    // Check if it's time to rotate keys
    checkKeyRotation();
    
    // Check if it's time for a periodic update (every 5 seconds)
    if (millis() - lastSentTime > 5000) {
      shouldSendUpdate = true;
    }
    
    // If button A is pressed or it's time for an update
    if (M5.BtnA.wasPressed() || shouldSendUpdate) {
      // Send regular update
      outgoingData.messageType = 0;
      outgoingData.counter++;
      outgoingData.value = 25.0 + (float)(outgoingData.counter % 10);
      sprintf(outgoingData.message, "Update from B: %d", outgoingData.counter);
      outgoingData.keyIndex = keyMgr.keyIndex;
      
      // Display data being sent
      M5.Lcd.fillRect(0, 40, 240, 40, BLACK); // Clear only send area
      M5.Lcd.setCursor(0, 40);
      M5.Lcd.setTextColor(CYAN, BLACK);
      M5.Lcd.println("SENDING TO DEVICE A:");
      M5.Lcd.setTextColor(WHITE, BLACK);
      M5.Lcd.printf("Type: Update, Count: %d\n", outgoingData.counter);
      
      // Send message via ESP-NOW
      esp_now_send(peerMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
      
      lastSentTime = millis();
      shouldSendUpdate = false;
    }
    
    // If button B is pressed
    if (M5.BtnB.wasPressed()) {
      // Send button press notification
      outgoingData.messageType = 1;
      sprintf(outgoingData.message, "Button B pressed!");
      outgoingData.keyIndex = keyMgr.keyIndex;
      
      // Display data being sent
      M5.Lcd.fillRect(0, 40, 240, 40, BLACK); // Clear only send area
      M5.Lcd.setCursor(0, 40);
      M5.Lcd.setTextColor(CYAN, BLACK);
      M5.Lcd.println("SENDING TO DEVICE A:");
      M5.Lcd.setTextColor(WHITE, BLACK);
      M5.Lcd.println("Type: Button Press Notification");
      
      // Send message via ESP-NOW
      esp_now_send(peerMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
      
      lastSentTime = millis();
    }
  }
  
  delay(50); // Small delay to prevent hogging the CPU
}