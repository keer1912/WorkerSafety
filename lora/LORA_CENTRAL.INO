// LoRa 9x_RX
// -*- mode:C++ -*-
// Example sketch showing how to create a simple messaging client (receiver)
// with the RH_RF95 class. RH_RF95 class does not provide for addressing or
// reliability, so you should only use RH_RF95 if you do not need the higher
// level messaging abilities.
// It is designed to work with the other example LoRa9x_TX

#include <SPI.h>
#include <RH_RF95.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

#define RFM95_CS 10
#define RFM95_RST 9
#define RFM95_INT 2

#define RF95_FREQ 915.0

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET    -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define I2COLED 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define RFM95_CS 10
#define RFM95_RST 9
#define RFM95_INT 2

#define node_id "CENTRAL"

const char* recipient_id = "SITE_A";

#pragma pack(push, 1)  // MUST match sender
struct Packet {
    char sender_id[10];
    char recipient_id[10];
    char payload[32];
    uint16_t checksum;
};
#pragma pack(pop)

// Singleton instance of the radio driver
RH_RF95 rf95(RFM95_CS, RFM95_INT);
 
void(* resetFunc) (void) = 0; //declare reset function at address 0

// Function to display message on OLED
void displayMessage(const char* message) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println(message);
  display.display();
}

void setup() 
{
  Serial.begin(9600);
  delay(100);

  // Initialize OLED display
  if(!display.begin(SSD1306_SWITCHCAPVCC, I2COLED)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  
  // Manual reset
  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);

  while (!rf95.init()) {
    Serial.println("LoRa radio init failed");
    displayMessage("Setup Failed");
    while (1);
  }
  displayMessage("Setup Successful");
  Serial.println("LoRa radio init OK!");

  // Defaults after init are 915.0MHz, modulation GFSK_Rb250Fd250, +13dbM
  if (!rf95.setFrequency(RF95_FREQ)) {
    Serial.println("setFrequency failed");
    displayMessage("setFrequency Failed");
    while (1);
  }
  Serial.print("Set Freq to: "); Serial.println(RF95_FREQ);

  // Defaults after init are 915.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on

  // The default transmitter power is 13dBm, using PA_BOOST.
  // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then 
  // you can set transmitter powers from 5 to 23 dBm:
  rf95.setTxPower(13, false);
}
 
int16_t packetnum = 0;  // packet counter, we increment per transmission

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

void loop() {
    if (rf95.available()) {
        uint8_t buf[sizeof(struct Packet)];
        uint8_t len = sizeof(buf);
        
        displayMessage("Waiting for Reply");

        if (rf95.recv(buf, &len)) {
            Packet* receivedPacket = (Packet*)buf;

            //Null-terminate the payload string
            receivedPacket->payload[sizeof(receivedPacket->payload) - 1] = '\0';

            // Check if the message is intended for this node
            if (strcmp(receivedPacket->recipient_id, node_id) == 0) {
                // Calculate and print checksum of the received packet
                uint16_t receivedChecksum = receivedPacket->checksum;
                uint16_t calculatedChecksum = calculateChecksum(receivedPacket);

                Serial.print("Received Checksum: ");
                Serial.println(receivedChecksum, DEC);
                Serial.print("Calculated Checksum: ");
                Serial.println(calculatedChecksum, DEC);

                if (receivedChecksum == calculatedChecksum){
                  Serial.print("Got valid message: ");
                  Serial.print("From "); Serial.print(receivedPacket->sender_id);
                  Serial.print(": "); Serial.println(receivedPacket->payload);
                  Serial.print("RSSI: ");
                  Serial.println(rf95.lastRssi(), DEC);

                  // Display the received message on the OLED
                  display.clearDisplay();
                  display.setTextSize(1);
                  display.setTextColor(SSD1306_WHITE);
                  display.setCursor(0, 0);
                  display.println("Message Received");
                  display.print("From "); display.print(receivedPacket->sender_id);
                  display.print(": "); display.println(receivedPacket->payload);
                  display.display();

                  // Send a reply
                  struct Packet replyPacket;
                  strncpy(replyPacket.sender_id, node_id, sizeof(replyPacket.sender_id) - 1);
                  strncpy(replyPacket.recipient_id, receivedPacket->sender_id, sizeof(replyPacket.recipient_id) - 1);
                  snprintf(replyPacket.payload, sizeof(replyPacket.payload), "ACK");
                  // Cal checksum
                  replyPacket.checksum = calculateChecksum(&replyPacket);

                  displayMessage("Sending Message");
                  Serial.print("Sending: ");
                  Serial.print("From "); Serial.print(replyPacket.sender_id);
                  Serial.print(" To "); Serial.print(replyPacket.recipient_id);
                  Serial.print(": "); Serial.println(replyPacket.payload);
                  Serial.print("With checksum: ");
                  Serial.println(replyPacket.checksum);

                  rf95.send((uint8_t*)&replyPacket, sizeof(replyPacket));
                  rf95.waitPacketSent();

                  Serial.println("Sent a reply");
                }
            } else {
                // Ignore messages not intended for this node
                Serial.print("Ignored message: ");
                Serial.print("From "); Serial.print(receivedPacket->sender_id);
                Serial.print(" To "); Serial.print(receivedPacket->recipient_id);
                Serial.print(": "); Serial.println(receivedPacket->payload);
            }
        } else {
            Serial.println("Receive failed");
        }
    }
}