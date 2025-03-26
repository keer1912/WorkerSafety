// ESP-NOW SECURE MESH NETWORK NODE
// Combined code for all nodes - change NODE_ID to 1, 2, or 3 before uploading

#include <M5StickCPlus.h>
#include <esp_now.h>
#include <WiFi.h>
#include <Preferences.h>

// *** CONFIGURATION SECTION ***
// Change this value before uploading to each device
#define NODE_ID 1  // Set to 1, 2, or 3 depending on which device you're programming

// Network configuration
#define MAX_PEERS 10        // Maximum number of peers to maintain
#define DISCOVERY_INTERVAL 30000  // Time between discovery broadcasts (ms)
#define PEER_TIMEOUT 60000  // Time before considering a peer inactive (ms)

// Message types
#define MSG_TYPE_DISCOVERY 1     // Node discovery broadcast
#define MSG_TYPE_DATA 2          // Regular data message
#define MSG_TYPE_HEARTBEAT 3     // Keep-alive message

// Mesh message structure
typedef struct {
  uint8_t messageType;     // Type of message
  uint8_t srcId;           // Source node ID
  uint8_t destId;          // Destination node ID (0 = broadcast)
  char payload[64];        // Message payload
  float value;             // Numeric value (for sensor data, etc.)
} MeshMessage;

// Peer information structure
typedef struct {
  uint8_t mac[6];          // MAC address of peer
  uint8_t nodeId;          // Node ID if known
  uint32_t lastSeen;       // Last time we received a message from this peer
  bool isActive;           // Is this peer currently active
} PeerInfo;

// Global variables
MeshMessage outgoingMsg;
MeshMessage incomingMsg;
PeerInfo knownPeers[MAX_PEERS];
uint16_t nextMessageId = 1;
unsigned long lastDiscoveryTime = 0;
unsigned long lastHeartbeatTime = 0;

// Forward declarations
void broadcastDiscovery();
void sendHeartbeat();
void updatePeerInfo(const uint8_t *mac, uint8_t nodeId);
void cleanInactivePeers();

// Callback function when data is sent
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

// Callback function when data is received
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  // Copy the data to our variable
  memcpy(&incomingMsg, data, sizeof(incomingMsg));
  
  // Update peer information
  updatePeerInfo(mac_addr, incomingMsg.srcId);
  
  // Process based on message type
  switch (incomingMsg.messageType) {
    case MSG_TYPE_DISCOVERY:
      handleDiscoveryMessage(mac_addr);
      break;
    case MSG_TYPE_DATA:
      handleDataMessage(mac_addr);
      break;
    case MSG_TYPE_HEARTBEAT:
      handleHeartbeat(mac_addr);
      break;
  }
}

// Update or add peer information
void updatePeerInfo(const uint8_t *mac, uint8_t nodeId) {
  int emptySlot = -1;
  
  // Look for existing peer or empty slot
  for (int i = 0; i < MAX_PEERS; i++) {
    if (knownPeers[i].isActive && memcmp(knownPeers[i].mac, mac, 6) == 0) {
      // Update existing peer
      knownPeers[i].lastSeen = millis();
      if (nodeId > 0) {
        knownPeers[i].nodeId = nodeId;
      }
      return;
    }
    
    if (emptySlot == -1 && !knownPeers[i].isActive) {
      emptySlot = i;
    }
  }
  
  // Add new peer if slot available
  if (emptySlot != -1) {
    memcpy(knownPeers[emptySlot].mac, mac, 6);
    knownPeers[emptySlot].nodeId = nodeId;
    knownPeers[emptySlot].lastSeen = millis();
    knownPeers[emptySlot].isActive = true;
    
    // Register this peer with ESP-NOW
    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = true;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      M5.Lcd.println("Failed to add peer");
      knownPeers[emptySlot].isActive = false;
    } else {
      M5.Lcd.printf("Added peer Node %d\n", nodeId);
    }
  }
}

// Clean up inactive peers
void cleanInactivePeers() {
  for (int i = 0; i < MAX_PEERS; i++) {
    if (knownPeers[i].isActive && 
        millis() - knownPeers[i].lastSeen > PEER_TIMEOUT) {
      knownPeers[i].isActive = false;
      esp_now_del_peer(knownPeers[i].mac);
    }
  }
}

// Send discovery broadcast
void broadcastDiscovery() {
  outgoingMsg.messageType = MSG_TYPE_DISCOVERY;
  outgoingMsg.srcId = NODE_ID;
  outgoingMsg.destId = 0; // Broadcast
  
  // Send to all active peers
  for (int i = 0; i < MAX_PEERS; i++) {
    if (knownPeers[i].isActive) {
      esp_now_send(knownPeers[i].mac, (uint8_t*)&outgoingMsg, sizeof(outgoingMsg));
    }
  }
  
  lastDiscoveryTime = millis();
}

// Send heartbeat to maintain peer connections
void sendHeartbeat() {
  outgoingMsg.messageType = MSG_TYPE_HEARTBEAT;
  outgoingMsg.srcId = NODE_ID;
  outgoingMsg.destId = 0; // Broadcast
  
  // Send to all active peers
  for (int i = 0; i < MAX_PEERS; i++) {
    if (knownPeers[i].isActive) {
      esp_now_send(knownPeers[i].mac, (uint8_t*)&outgoingMsg, sizeof(outgoingMsg));
    }
  }
  
  lastHeartbeatTime = millis();
}

// Handle discovery message
void handleDiscoveryMessage(const uint8_t *mac_addr) {
  // Forward discovery to other peers
  for (int i = 0; i < MAX_PEERS; i++) {
    if (knownPeers[i].isActive && memcmp(knownPeers[i].mac, mac_addr, 6) != 0) {
      esp_now_send(knownPeers[i].mac, (uint8_t*)&incomingMsg, sizeof(incomingMsg));
    }
  }
}

// Handle data message
void handleDataMessage(const uint8_t *mac_addr) {
  // If we're the destination, display the message
  if (incomingMsg.destId == NODE_ID || incomingMsg.destId == 0) {
    M5.Lcd.fillRect(0, 60, 240, 40, BLACK);
    M5.Lcd.setCursor(0, 60);
    M5.Lcd.setTextColor(YELLOW, BLACK);
    M5.Lcd.printf("MSG from Node %d:\n", incomingMsg.srcId);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.printf("%s (%.1f)\n", incomingMsg.payload, incomingMsg.value);
  }
  
  // Forward broadcast or messages meant for other nodes
  if (incomingMsg.destId != NODE_ID) {
    for (int i = 0; i < MAX_PEERS; i++) {
      if (knownPeers[i].isActive && memcmp(knownPeers[i].mac, mac_addr, 6) != 0) {
        esp_now_send(knownPeers[i].mac, (uint8_t*)&incomingMsg, sizeof(incomingMsg));
      }
    }
  }
}

// Handle heartbeat message
void handleHeartbeat(const uint8_t *mac_addr) {
  // Update peer's last seen time
  for (int i = 0; i < MAX_PEERS; i++) {
    if (knownPeers[i].isActive && memcmp(knownPeers[i].mac, mac_addr, 6) == 0) {
      knownPeers[i].lastSeen = millis();
      break;
    }
  }
}

// Initialize ESP-NOW
void initESPNOW() {
  WiFi.mode(WIFI_STA);
  
  if (esp_now_init() != ESP_OK) {
    M5.Lcd.println("Error initializing ESP-NOW");
    return;
  }
  
  // Register callbacks
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  // Initialize peer list
  for (int i = 0; i < MAX_PEERS; i++) {
    knownPeers[i].isActive = false;
  }
}

void setup() {
  // Initialize the M5StickC Plus
  M5.begin();
  M5.Lcd.setRotation(3); // Landscape mode
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("ESP-NOW MESH");
  M5.Lcd.printf("NODE ID: %d\n", NODE_ID);
  
  // Initialize ESP-NOW
  initESPNOW();
  
  // Display MAC Address
  M5.Lcd.println("MAC: " + WiFi.macAddress());
  
  // Initial discovery broadcast
  broadcastDiscovery();
  
  // Show instructions
  M5.Lcd.setCursor(0, 130);
  M5.Lcd.println("Btn A: Send data");
  M5.Lcd.println("Btn B: Broadcast discovery");
}

void loop() {
  M5.update(); // Update button state
  
  // Periodic discovery broadcast
  if (millis() - lastDiscoveryTime > DISCOVERY_INTERVAL) {
    broadcastDiscovery();
  }
  
  // Periodic heartbeat
  if (millis() - lastHeartbeatTime > DISCOVERY_INTERVAL / 3) {
    sendHeartbeat();
  }
  
  // Clean up inactive peers
  if (millis() % 60000 < 100) {  // Approximately every minute
    cleanInactivePeers();
  }
  
  // Button A: Send data to all peers
  if (M5.BtnA.wasPressed()) {
    char message[64];
    sprintf(message, "Hello from Node %d", NODE_ID);
    
    outgoingMsg.messageType = MSG_TYPE_DATA;
    outgoingMsg.srcId = NODE_ID;
    outgoingMsg.destId = 0; // Broadcast
    strcpy(outgoingMsg.payload, message);
    outgoingMsg.value = analogRead(36) / 4095.0 * 100.0;
    
    // Send to all active peers
    for (int i = 0; i < MAX_PEERS; i++) {
      if (knownPeers[i].isActive) {
        esp_now_send(knownPeers[i].mac, (uint8_t*)&outgoingMsg, sizeof(outgoingMsg));
      }
    }
  }
  
  // Button B: Trigger discovery broadcast manually
  if (M5.BtnB.wasPressed()) {
    broadcastDiscovery();
  }
  
  delay(50);  // Small delay to prevent hogging the CPU
} 