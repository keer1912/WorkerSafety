// BIDIRECTIONAL ESP-NOW COMMUNICATION WITH FALL DETECTION - DEVICE A

#include <M5StickCPlus.h>
#include <esp_now.h>
#include <WiFi.h>
#include <Preferences.h>  // For storing keys in non-volatile memory

// REPLACE WITH THE MAC Address of your second device (Device 😎
uint8_t peerMacAddress[] = {0x4C, 0x75, 0x25, 0xCB, 0x90, 0x88};  // Replace with actual MAC

// Create a preferences object for storing encryption keys
Preferences preferences;

// Define a structure for key management
typedef struct {
  uint8_t currentKey[16];
  uint32_t keyRotationTime;  // Unix timestamp for next rotation
  uint16_t keyIndex;         // Current key index/version
} KeyManager;

KeyManager keyMgr;

// Define a data structure for communication
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

// Send emergency fall detection notification
void sendFallEmergency() {
  // Create an emergency message
  outgoingData.messageType = 4; // Fall emergency notification
  sprintf(outgoingData.message, "EMERGENCY: WORKER FALLEN!");
  outgoingData.value = accelMagnitude; // Send impact force
  outgoingData.keyIndex = keyMgr.keyIndex;
  
  // Display emergency message
  M5.Lcd.fillRect(0, 40, 240, 40, BLACK);
  M5.Lcd.setCursor(0, 40);
  M5.Lcd.setTextColor(RED, BLACK);
  M5.Lcd.println("SENDING EMERGENCY ALERT:");
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.println("Fall detected, help needed!");
  
  // Send emergency message via ESP-NOW
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
    M5.Lcd.println("WORKER B HAS FALLEN");
    M5.Lcd.setTextSize(1);
    M5.Lcd.printf("Impact force: %.2f\n", incomingData.value);
    
    // Play alarm sound
    for (int i = 0; i < 5; i++) {
      M5.Beep.beep();
      delay(300);
      M5.Beep.mute();
      delay(100);
    }
    
    // Send acknowledgment
    outgoingData.messageType = 2;
    sprintf(outgoingData.message, "Emergency received, help coming");
    esp_now_send(peerMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
    return;
  }
  
  // Handle regular messages
  M5.Lcd.fillRect(0, 80, 240, 40, BLACK); // Clear only receive area
  M5.Lcd.setCursor(0, 80);
  M5.Lcd.setTextColor(YELLOW, BLACK);
  M5.Lcd.println("RECEIVED FROM DEVICE B:");
  M5.Lcd.setTextColor(WHITE, BLACK);
  
  // Display different info based on message type
  switch(incomingData.messageType) {
    case 0:
      M5.Lcd.printf("Update: %s, Count: %d\n", incomingData.message, incomingData.counter);
      break;
    case 1:
      M5.Lcd.printf("Button pressed on Device B\n");
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
        M5.Lcd.println("FALL DETECTED!");
        
        M5.Beep.beep();  // Alert sound
        delay(1000);
        M5.Beep.mute();
        
        // Send emergency message to Device B
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
  M5.Lcd.println("M5StickC Plus ESP-NOW");
  M5.Lcd.println("DEVICE A with Fall Detection");
  
  // Initialize random number generator
  randomSeed(analogRead(0));
  
  // Display MAC Address
  WiFi.mode(WIFI_STA);
  M5.Lcd.println("MAC: " + WiFi.macAddress());
  M5.Lcd.println("Use this MAC for Device B!");
  
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
  strcpy(outgoingData.message, "Hello from Device A");
  outgoingData.counter = 0;
  outgoingData.value = 22.5;
  outgoingData.keyIndex = keyMgr.keyIndex;
  
  // Initialize moving average for fall detection
  for (int i = 0; i < movingAverageWindow; i++) {
    accelMagnitudeHistory[i] = 1.0; // Start with gravity (1G)
  }
  
  // Show instructions
  M5.Lcd.setCursor(0, 130);
  M5.Lcd.println("Btn A: Send update/Recover");
  M5.Lcd.println("Btn B: Notify button press");
}

void loop() {
  M5.update(); // Update button state
  
  // Check if it's time to rotate keys
  checkKeyRotation();
  
  // Check for falls
  detectFall();
  
  // Check if it's time for a periodic update (every 5 seconds)
  if (millis() - lastSentTime > 5000) {
    shouldSendUpdate = true;
  }
  
  // If button A is pressed or it's time for an update
  if (M5.BtnA.wasPressed() || shouldSendUpdate) {
    // Only send if not in FALLEN state (to avoid conflict with recovery function)
    if (userState != FALLEN || shouldSendUpdate) {
      // Send regular update
      outgoingData.messageType = 0;
      outgoingData.counter++;
      outgoingData.value = 22.5 + (float)(outgoingData.counter % 10);
      sprintf(outgoingData.message, "Update from A: %d", outgoingData.counter);
      outgoingData.keyIndex = keyMgr.keyIndex;
      
      // Display data being sent
      M5.Lcd.fillRect(0, 40, 240, 40, BLACK); // Clear only send area
      M5.Lcd.setCursor(0, 40);
      M5.Lcd.setTextColor(CYAN, BLACK);
      M5.Lcd.println("SENDING TO DEVICE B:");
      M5.Lcd.setTextColor(WHITE, BLACK);
      M5.Lcd.printf("Type: Update, Count: %d\n", outgoingData.counter);
      
      // Send message via ESP-NOW
      esp_now_send(peerMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
      
      lastSentTime = millis();
      shouldSendUpdate = false;
    }
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
    M5.Lcd.println("SENDING TO DEVICE B:");
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.println("Type: Button Press Notification");
    
    // Send message via ESP-NOW
    esp_now_send(peerMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
    
    lastSentTime = millis();
  }
  
  delay(50); // Small delay to prevent hogging the CPU
}