// SENDER DEVICE (First M5StickC Plus)
// File name: M5StickC_ESP_NOW_Sender.ino

#include <M5StickCPlus.h>
#include <esp_now.h>
#include <WiFi.h>

// REPLACE WITH THE MAC Address of your receiver
uint8_t receiverMacAddress[] = {0x4C, 0x75, 0x25, 0xCB, 0x90, 0x88};  // Replace with actual MAC

// Define a data structure
typedef struct message_struct {
  char message[32];
  int counter;
  float temperature;
} message_struct;

// Create a structured object
message_struct myData;

// Peer info
esp_now_peer_info_t peerInfo;

// Callback function called when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  M5.Lcd.setCursor(0, 80);
  if (status == ESP_NOW_SEND_SUCCESS) {
    M5.Lcd.setTextColor(GREEN, BLACK);
    M5.Lcd.println("Delivery Success");
  } else {
    M5.Lcd.setTextColor(RED, BLACK);
    M5.Lcd.println("Delivery Fail");
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
  M5.Lcd.println("SENDER");
  
  // Display MAC Address
  WiFi.mode(WIFI_STA);
  M5.Lcd.println("MAC: " + WiFi.macAddress());
  
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    M5.Lcd.println("Error initializing ESP-NOW");
    return;
  }
  
  // Register the send callback
  esp_now_register_send_cb(OnDataSent);
  
  // Register peer
  memcpy(peerInfo.peer_addr, receiverMacAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  // Add peer        
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    M5.Lcd.println("Failed to add peer");
    return;
  }
  
  // Initialize data
  strcpy(myData.message, "Hello from M5StickC Plus");
  myData.counter = 0;
  myData.temperature = 23.5;
}

void loop() {
  M5.update(); // Update button state
  
  // Increment counter
  myData.counter++;
  myData.temperature = myData.temperature + 0.1;
  
  // Display data being sent
  M5.Lcd.setCursor(0, 40);
  M5.Lcd.printf("Sending: %s\n", myData.message);
  M5.Lcd.printf("Counter: %d\n", myData.counter);
  M5.Lcd.printf("Temp: %.1fC\n", myData.temperature);
  
  // Send message via ESP-NOW
  esp_err_t result = esp_now_send(receiverMacAddress, (uint8_t *) &myData, sizeof(myData));
  
  if (result == ESP_OK) {
    M5.Lcd.println("Sent with success");
  } else {
    M5.Lcd.println("Error sending the data");
  }
  
  // If button A is pressed, reset counter
  if (M5.BtnA.wasPressed()) {
    myData.counter = 0;
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("M5StickC Plus ESP-NOW");
    M5.Lcd.println("SENDER");
    M5.Lcd.println("MAC: " + WiFi.macAddress());
    M5.Lcd.println("Counter Reset!");
  }
  
  delay(2000); // Wait for 2 seconds before next transmission
}