#include <SPI.h>
#include <RH_RF95.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

// Configuration constants
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define RFM95_CS 10
#define RFM95_RST 9
#define RFM95_INT 2
#define node_id "SITE_A"
#define I2COLED 0x3C
#define RF95_FREQ 915.0
#define SERIAL_BUFFER_SIZE 64  // Reduced from dynamic String
#define MAX_DISPLAY_LINES 3

// OLED display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Radio driver
RH_RF95 rf95(RFM95_CS, RFM95_INT);

// Packet structure - reduced field sizes
#pragma pack(push, 1)
struct Packet {
    char sender_id[10];      // Reduced from 10
    char recipient_id[10];   // Reduced from 10
    char payload[32];       // Reduced from 32
    uint16_t checksum;
};
#pragma pack(pop)

// Global buffers
char serialBuffer[SERIAL_BUFFER_SIZE];
uint8_t serialBufferIndex = 0;
char displayBuffer[MAX_DISPLAY_LINES][21]; // For 3 lines of 20 chars + null

// Statistics - made volatile for better memory management
volatile unsigned long lastStatusUpdate = 0;
volatile uint16_t receivedCharCount = 0;
volatile uint16_t receivedNewlines = 0;
volatile uint16_t processedMessages = 0;
volatile uint16_t blankMessages = 0;

// Function to calculate checksum (sum of ASCII values of the entire packet)
uint16_t calculateChecksum(const struct Packet* packet) {
    uint16_t checksum = 0;

    // Sum ASCII values of sender_id
    for (int i = 0; i < sizeof(packet->sender_id); i++) {
        checksum += packet->sender_id[i];
    }

    // Sum ASCII values of recipient_id
    for (int i = 0; i < sizeof(packet->recipient_id); i++) {
        checksum += packet->recipient_id[i];
    }

    // Sum ASCII values of payload
    for (int i = 0; i < sizeof(packet->payload); i++) {
        checksum += packet->payload[i];
    }

    return checksum;  // Return the calculated checksum
}

// Optimized display function
void displayLines(const char *lines[], uint8_t count) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    for (uint8_t i = 0; i < count && i < MAX_DISPLAY_LINES; i++) {
        display.setCursor(0, i * 10);
        display.println(lines[i]);
    }
    display.display();
}

void setup() {
    Serial.begin(9600);
    while (!Serial) delay(10);

    // Initialize OLED
    if (!display.begin(SSD1306_SWITCHCAPVCC, I2COLED)) {
        Serial.println(F("OLED allocation failed"));
        while(1);
    }
    display.clearDisplay();

    // Radio initialization
    pinMode(RFM95_RST, OUTPUT);
    digitalWrite(RFM95_RST, HIGH);
    delay(10);
    digitalWrite(RFM95_RST, LOW);
    delay(10);
    digitalWrite(RFM95_RST, HIGH);
    delay(10);

    if (!rf95.init()) {
        const char *lines[] = {"Setup Failed", "Radio Init Error", ""};
        displayLines(lines, 2);
        while(1);
    }

    if (!rf95.setFrequency(RF95_FREQ)) {
        const char *lines[] = {"Setup Failed", "Freq Error", ""};
        displayLines(lines, 2);
        while(1);
    }

    rf95.setTxPower(13, false);
    rf95.setModemConfig(RH_RF95::Bw125Cr45Sf128);

    const char *lines[] = {"Ready", "Awaiting Data", "BufLen: 0"};
    displayLines(lines, 3);
}

void processSerialData(const char *input) {
    if (strlen(input) == 0) {
        blankMessages++;
        const char *lines[] = {"ERROR", "Blank message", ""};
        displayLines(lines, 2);
        return;
    }

    processedMessages++;

    // Display received data (optional)
    const char *lines[] = {"Received:", input, ""};
    displayLines(lines, 2);

    // Prepare LoRa packet
    Packet radiopacket;
    memset(&radiopacket, 0, sizeof(radiopacket));

    // Copy data with guaranteed null-termination
    strncpy(radiopacket.sender_id, node_id, sizeof(radiopacket.sender_id)-1);
    strncpy(radiopacket.recipient_id, "CENTRAL", sizeof(radiopacket.recipient_id)-1);
    strncpy(radiopacket.payload, input, sizeof(radiopacket.payload)-1);
    
    // Explicit null-termination
    radiopacket.sender_id[sizeof(radiopacket.sender_id)-1] = '\0';
    radiopacket.recipient_id[sizeof(radiopacket.recipient_id)-1] = '\0';
    radiopacket.payload[sizeof(radiopacket.payload)-1] = '\0';

    // Calculate checksum AFTER all fields are set
    radiopacket.checksum = calculateChecksum(&radiopacket);

    // Send packet
    rf95.send((uint8_t *)&radiopacket, sizeof(radiopacket));
    rf95.waitPacketSent();

    Serial.print("Sent raw: ");
    Serial.println(input);
}

void loop() {
  // Periodic status update
  if (millis() - lastStatusUpdate > 10000) {
      lastStatusUpdate = millis();
      char stats[21];
      snprintf(stats, sizeof(stats), "C:%d P:%d", receivedCharCount, processedMessages);
      const char *lines[] = {"Status", stats, ""};
      displayLines(lines, 2);
  }

  // Read serial data
  while (Serial.available() > 0 && serialBufferIndex < SERIAL_BUFFER_SIZE - 1) {
      char c = Serial.read();
      receivedCharCount++;

      if (c == '\n' || c == '\r') {
          if (serialBufferIndex > 0) {
              serialBuffer[serialBufferIndex] = '\0'; // Null-terminate
              processSerialData(serialBuffer); // Send raw data
              serialBufferIndex = 0; // Reset buffer
          }
      } else if (isPrintable(c)) {
          serialBuffer[serialBufferIndex++] = c; // Store character
      }
  }

  // LoRa Receive Handling
  if (rf95.available()) {
    uint8_t buf[sizeof(Packet)];
    uint8_t len = sizeof(buf);
    
    if (rf95.recv(buf, &len)) {
      Packet* received = (Packet*)buf;
      if (strcmp(received->recipient_id, node_id) == 0 &&
          calculateChecksum(received) == received->checksum) {
            const char *lines[] = {"ACK:", received->payload};
          displayLines(lines, received->payload);
      }
    }
  }
}