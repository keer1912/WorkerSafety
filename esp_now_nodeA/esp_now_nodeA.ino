
// BIDIRECTIONAL ESP-NOW COMMUNICATION - DEVICE A
// File name: M5StickC_ESP_NOW_DeviceA.ino

#include <M5StickCPlus.h>
#include <esp_now.h>
#include <WiFi.h>

// REPLACE WITH THE MAC Address of your second device (Device 😎
uint8_t peerMacAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // Replace with actual MAC

// Define a data structure
typedef struct message_struct {
  uint8_t messageType;  // 0 = regular update, 1 = button press notification, 2 = response
  char message[32];
  int counter;
  float value;
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
  
  // Update the display with received data
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

void setup() {
  // Initialize the M5StickC Plus
  M5.begin();
  M5.Lcd.setRotation(3); // Landscape mode
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("DEVICE A");
  
  // Display MAC Address
  WiFi.mode(WIFI_STA);
  M5.Lcd.println("MAC: " + WiFi.macAddress());
  
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    M5.Lcd.println("Error initializing ESP-NOW");
    return;
  }
  
  // Register callbacks
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  // Register peer
  memcpy(peerInfo.peer_addr, peerMacAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
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
  
  // Show instructions
  M5.Lcd.setCursor(0, 130);
  M5.Lcd.println("Btn A: Send update");
  M5.Lcd.println("Btn B: Notify button press");
}

void loop() {
  M5.update(); // Update button state
  
  // Check if it's time for a periodic update (every 5 seconds)
  if (millis() - lastSentTime > 5000) {
    shouldSendUpdate = true;
  }
  
  // If button A is pressed or it's time for an update
  if (M5.BtnA.wasPressed() || shouldSendUpdate) {
    // Send regular update
    outgoingData.messageType = 0;
    outgoingData.counter++;
    outgoingData.value = 22.5 + (float)(outgoingData.counter % 10);
    sprintf(outgoingData.message, "Update from A: %d", outgoingData.counter);
    
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
  
  // If button B is pressed
  if (M5.BtnB.wasPressed()) {
    // Send button press notification
    outgoingData.messageType = 1;
    sprintf(outgoingData.message, "Button B pressed!");
    
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