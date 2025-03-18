// RECEIVER DEVICE (Second M5StickC Plus)
// File name: M5StickC_ESP_NOW_Receiver.ino

#include <M5StickCPlus.h>
#include <esp_now.h>
#include <WiFi.h>

// Define the same data structure as in the sender
typedef struct message_struct {
  char message[32];
  int counter;
  float temperature;
} message_struct;

// Create a structured object to hold incoming data
message_struct incomingData;

// Last received values
int lastCounter = 0;
unsigned long lastReceivedTime = 0;

// Callback function called when data is received
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  
  // Copy the data to our variable
  memcpy(&incomingData, data, sizeof(incomingData));
  
  // Update the display with received data
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("M5StickC Plus ESP-NOW");
  M5.Lcd.println("RECEIVER");
  M5.Lcd.println("MAC: " + WiFi.macAddress());
  
  M5.Lcd.setCursor(0, 40);
  M5.Lcd.setTextColor(YELLOW, BLACK);
  M5.Lcd.printf("From: %s\n", macStr);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.printf("Message: %s\n", incomingData.message);
  M5.Lcd.printf("Counter: %d\n", incomingData.counter);
  M5.Lcd.printf("Temp: %.1fC\n", incomingData.temperature);
  
  // Update variables
  lastCounter = incomingData.counter;
  lastReceivedTime = millis();
}

void setup() {
  // Initialize the M5StickC Plus
  M5.begin();
  M5.Lcd.setRotation(3); // Landscape mode
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("M5StickC Plus ESP-NOW");
  M5.Lcd.println("RECEIVER");
  
  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  
  // Display MAC Address (needed for the sender sketch)
  M5.Lcd.println("MAC: " + WiFi.macAddress());
  
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    M5.Lcd.println("Error initializing ESP-NOW");
    return;
  }
  
  // Register the receive callback
  esp_now_register_recv_cb(OnDataRecv);
}

void loop() {
  M5.update(); // Update button state
  
  // Check if we've received any data recently
  if (millis() - lastReceivedTime > 5000) {
    // No data received for 5 seconds
    M5.Lcd.setCursor(0, 120);
    M5.Lcd.setTextColor(RED, BLACK);
    M5.Lcd.println("No data received...");
  }
  
  // If button A is pressed, clear the screen
  if (M5.BtnA.wasPressed()) {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.println("M5StickC Plus ESP-NOW");
    M5.Lcd.println("RECEIVER");
    M5.Lcd.println("MAC: " + WiFi.macAddress());
    M5.Lcd.println("Waiting for data...");
  }
  
  delay(100); // Small delay to prevent hogging the CPU
}