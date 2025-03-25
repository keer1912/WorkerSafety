// ESP-NOW MESH NETWORK NODE
// Based on existing bidirectional ESP-NOW implementation

#include <M5StickCPlus.h>
#include <esp_now.h>
#include <WiFi.h>
#include <Preferences.h>

// Maximum number of peers in mesh network
#define MAX_MESH_PEERS 20

// Node configuration
#define NODE_ID 1  // IMPORTANT: Each node must have a unique ID (1, 2, 3, etc.)
const char* NODE_NAME = "Node1"; // Human-readable name

// Mesh network configuration
#define MAX_HOP_COUNT 5    // Maximum number of hops a message can travel
#define DISCOVERY_INTERVAL 30000  // Time between mesh discovery broadcasts (ms)
#define ROUTE_EXPIRY_TIME 300000  // Time before routes expire (ms)

// Message types
#define MSG_TYPE_DATA 0          // Regular data message
#define MSG_TYPE_DISCOVERY 1     // Node discovery broadcast
#define MSG_TYPE_ROUTE_REQUEST 2 // Request for a route to a destination
#define MSG_TYPE_ROUTE_REPLY 3   // Reply with route information
#define MSG_TYPE_HEARTBEAT 4     // Periodic heartbeat to keep routes alive

// Routing table entry
typedef struct {
  uint8_t destId;          // Destination node ID
  uint8_t nextHopMac[6];   // MAC address of the next hop
  uint8_t hopCount;        // Number of hops to destination
  uint32_t lastUpdated;    // Last time this route was updated
  bool isActive;           // Is this route currently active
} RouteEntry;

// Mesh message structure
typedef struct {
  uint8_t messageType;     // Type of message (see defines above)
  uint8_t srcId;           // Source node ID
  uint8_t destId;          // Destination node ID (0 = broadcast)
  uint8_t origSrcId;       // Original source ID (for forwarded messages)
  uint8_t hopCount;        // Current hop count
  uint16_t messageId;      // Unique message ID to prevent loops
  char payload[64];        // Message payload
  float value;             // Numeric value (for sensor data, etc.)
} MeshMessage;

// Known peers in the mesh
typedef struct {
  uint8_t mac[6];          // MAC address of peer
  uint8_t nodeId;          // Node ID if known, 0 if unknown
  uint32_t lastSeen;       // Last time we received a message from this peer
  bool isDirect;           // Is this a direct (one-hop) peer
} MeshPeer;

// Global variables
MeshMessage outgoingMsg;
MeshMessage incomingMsg;
MeshPeer knownPeers[MAX_MESH_PEERS];
RouteEntry routingTable[MAX_MESH_PEERS];
uint16_t nextMessageId = 1;
unsigned long lastDiscoveryTime = 0;
unsigned long lastHeartbeatTime = 0;

// Message history to prevent loops
#define MAX_MESSAGE_HISTORY 20
struct {
  uint8_t srcId;
  uint16_t messageId;
  uint32_t timestamp;
} messageHistory[MAX_MESSAGE_HISTORY];
uint8_t historyIndex = 0;

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
  
  // Update the peer record
  updatePeerInfo(mac_addr, incomingMsg.srcId);
  
  // Check if we've seen this message before (to prevent loops)
  if (isMessageInHistory(incomingMsg.origSrcId, incomingMsg.messageId)) {
    // We've already processed this message, ignore it
    return;
  }
  
  // Add message to history
  addMessageToHistory(incomingMsg.origSrcId, incomingMsg.messageId);
  
  // Process based on message type
  switch (incomingMsg.messageType) {
    case MSG_TYPE_DISCOVERY:
      handleDiscoveryMessage(mac_addr);
      break;
      
    case MSG_TYPE_ROUTE_REQUEST:
      handleRouteRequest(mac_addr);
      break;
      
    case MSG_TYPE_ROUTE_REPLY:
      handleRouteReply();
      break;
      
    case MSG_TYPE_DATA:
      handleDataMessage(mac_addr);
      break;
      
    case MSG_TYPE_HEARTBEAT:
      handleHeartbeat(mac_addr);
      break;
  }
}

// Initialize ESP-NOW and register callbacks
void initESPNOW() {
  WiFi.mode(WIFI_STA);
  
  if (esp_now_init() != ESP_OK) {
    M5.Lcd.println("Error initializing ESP-NOW");
    return;
  }
  
  // Register callbacks
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  // Initialize mesh state
  initMesh();
}

// Initialize mesh-related data structures
void initMesh() {
  // Clear routing table
  for (int i = 0; i < MAX_MESH_PEERS; i++) {
    routingTable[i].isActive = false;
    knownPeers[i].nodeId = 0;
    knownPeers[i].lastSeen = 0;
    knownPeers[i].isDirect = false;
  }
  
  // Initialize message history
  for (int i = 0; i < MAX_MESSAGE_HISTORY; i++) {
    messageHistory[i].srcId = 0;
    messageHistory[i].messageId = 0;
    messageHistory[i].timestamp = 0;
  }
}

// Update info about a peer based on received message
void updatePeerInfo(const uint8_t *mac, uint8_t nodeId) {
  int emptySlot = -1;
  
  // Look for existing peer or empty slot
  for (int i = 0; i < MAX_MESH_PEERS; i++) {
    if (knownPeers[i].lastSeen > 0 && memcmp(knownPeers[i].mac, mac, 6) == 0) {
      // Update existing peer
      knownPeers[i].lastSeen = millis();
      if (nodeId > 0) {
        knownPeers[i].nodeId = nodeId;
      }
      knownPeers[i].isDirect = true;
      return;
    }
    
    if (emptySlot == -1 && knownPeers[i].lastSeen == 0) {
      emptySlot = i;
    }
  }
  
  // Add new peer if slot available
  if (emptySlot != -1) {
    memcpy(knownPeers[emptySlot].mac, mac, 6);
    knownPeers[emptySlot].nodeId = nodeId;
    knownPeers[emptySlot].lastSeen = millis();
    knownPeers[emptySlot].isDirect = true;
    
    // Register this peer with ESP-NOW
    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
  }
}

// Send a message to a specific peer or broadcast
bool sendMeshMessage(uint8_t destId, uint8_t msgType, const char* payload, float value) {
  // Prepare the message
  outgoingMsg.messageType = msgType;
  outgoingMsg.srcId = NODE_ID;
  outgoingMsg.destId = destId;
  outgoingMsg.origSrcId = NODE_ID;
  outgoingMsg.hopCount = 0;
  outgoingMsg.messageId = nextMessageId++;
  outgoingMsg.value = value;
  
  if (payload != NULL) {
    strncpy(outgoingMsg.payload, payload, sizeof(outgoingMsg.payload));
    outgoingMsg.payload[sizeof(outgoingMsg.payload) - 1] = '\0';
  } else {
    outgoingMsg.payload[0] = '\0';
  }
  
  // Add to our own message history to prevent loops
  addMessageToHistory(NODE_ID, outgoingMsg.messageId);
  
  // If it's a broadcast, send to all direct peers
  if (destId == 0) {
    bool success = false;
    for (int i = 0; i < MAX_MESH_PEERS; i++) {
      if (knownPeers[i].lastSeen > 0 && knownPeers[i].isDirect) {
        if (esp_now_send(knownPeers[i].mac, (uint8_t*)&outgoingMsg, sizeof(outgoingMsg)) == ESP_OK) {
          success = true;
        }
      }
    }
    return success;
  }
  
  // If it's a direct message, find route
  for (int i = 0; i < MAX_MESH_PEERS; i++) {
    if (routingTable[i].isActive && routingTable[i].destId == destId) {
      // Send to next hop in route
      return (esp_now_send(routingTable[i].nextHopMac, (uint8_t*)&outgoingMsg, sizeof(outgoingMsg)) == ESP_OK);
    }
  }
  
  // No route found, initiate route discovery
  initiateRouteDiscovery(destId);
  return false;
}

// Forward a received message
void forwardMessage(const MeshMessage* msg) {
  // Copy message and increment hop count
  MeshMessage fwdMsg;
  memcpy(&fwdMsg, msg, sizeof(MeshMessage));
  fwdMsg.hopCount++;
  fwdMsg.srcId = NODE_ID; // We are the new source
  
  // Don't forward if max hop count exceeded
  if (fwdMsg.hopCount >= MAX_HOP_COUNT) {
    return;
  }
  
  // If it's a broadcast, send to all peers except the one we received from
  if (msg->destId == 0) {
    for (int i = 0; i < MAX_MESH_PEERS; i++) {
      if (knownPeers[i].lastSeen > 0 && knownPeers[i].isDirect) {
        // Don't send back to original sender
        if (msg->srcId != knownPeers[i].nodeId) {
          esp_now_send(knownPeers[i].mac, (uint8_t*)&fwdMsg, sizeof(fwdMsg));
        }
      }
    }
    return;
  }
  
  // For directed messages, look up route
  for (int i = 0; i < MAX_MESH_PEERS; i++) {
    if (routingTable[i].isActive && routingTable[i].destId == msg->destId) {
      esp_now_send(routingTable[i].nextHopMac, (uint8_t*)&fwdMsg, sizeof(fwdMsg));
      return;
    }
  }
}

// Handle a discovery broadcast
void handleDiscoveryMessage(const uint8_t *mac_addr) {
  // Discovery messages update routing info
  updateRoute(incomingMsg.srcId, mac_addr, 1);
  
  // We might want to rebroadcast this discovery to help build mesh
  if (incomingMsg.hopCount < MAX_HOP_COUNT - 1) {
    forwardMessage(&incomingMsg);
  }
}

// Handle a route request
void handleRouteRequest(const uint8_t *mac_addr) {
  // If we are the destination, send a reply
  if (incomingMsg.destId == NODE_ID) {
    sendRouteReply(mac_addr, incomingMsg.origSrcId);
    return;
  }
  
  // Update route to requester
  updateRoute(incomingMsg.origSrcId, mac_addr, incomingMsg.hopCount);
  
  // Forward the request if we don't have max hops
  if (incomingMsg.hopCount < MAX_HOP_COUNT - 1) {
    forwardMessage(&incomingMsg);
  }
}

// Handle a route reply
void handleRouteReply() {
  // Update our routing table with this info
  updateRoute(incomingMsg.origSrcId, NULL, incomingMsg.hopCount);
  
  // If we're not the destination, forward it
  if (incomingMsg.destId != NODE_ID) {
    forwardMessage(&incomingMsg);
  }
}

// Handle a data message
void handleDataMessage(const uint8_t *mac_addr) {
  // If we're the destination, process it
  if (incomingMsg.destId == NODE_ID || incomingMsg.destId == 0) {
    M5.Lcd.fillRect(0, 60, 240, 40, BLACK);
    M5.Lcd.setCursor(0, 60);
    M5.Lcd.setTextColor(YELLOW, BLACK);
    M5.Lcd.printf("MSG from Node %d:\n", incomingMsg.origSrcId);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.printf("%s (%.1f)\n", incomingMsg.payload, incomingMsg.value);
    
    // No need to forward if we're the destination
    if (incomingMsg.destId == NODE_ID) {
      return;
    }
  }
  
  // Forward broadcast or messages meant for other nodes
  if (incomingMsg.destId != NODE_ID) {
    forwardMessage(&incomingMsg);
  }
}

// Handle a heartbeat message
void handleHeartbeat(const uint8_t *mac_addr) {
  // Update our routing info based on heartbeat
  updateRoute(incomingMsg.srcId, mac_addr, 1);
}

// Update routing table with new information
void updateRoute(uint8_t destId, const uint8_t *nextHopMac, uint8_t hopCount) {
  int emptySlot = -1;
  
  // Look for existing route or empty slot
  for (int i = 0; i < MAX_MESH_PEERS; i++) {
    if (routingTable[i].isActive && routingTable[i].destId == destId) {
      // Update existing route if better
      if (nextHopMac != NULL && (hopCount < routingTable[i].hopCount || 
          millis() - routingTable[i].lastUpdated > ROUTE_EXPIRY_TIME / 2)) {
        memcpy(routingTable[i].nextHopMac, nextHopMac, 6);
        routingTable[i].hopCount = hopCount;
      }
      routingTable[i].lastUpdated = millis();
      return;
    }
    
    if (emptySlot == -1 && !routingTable[i].isActive) {
      emptySlot = i;
    }
  }
  
  // Add new route if slot available and we have next hop info
  if (emptySlot != -1 && nextHopMac != NULL) {
    routingTable[emptySlot].destId = destId;
    memcpy(routingTable[emptySlot].nextHopMac, nextHopMac, 6);
    routingTable[emptySlot].hopCount = hopCount;
    routingTable[emptySlot].lastUpdated = millis();
    routingTable[emptySlot].isActive = true;
  }
}

// Start route discovery process
void initiateRouteDiscovery(uint8_t destId) {
  M5.Lcd.fillRect(0, 40, 240, 20, BLACK);
  M5.Lcd.setCursor(0, 40);
  M5.Lcd.printf("Discovering route to %d...\n", destId);
  
  // Send route request as broadcast
  sendMeshMessage(0, MSG_TYPE_ROUTE_REQUEST, "", destId);
}

// Send a route reply to a specific node
void sendRouteReply(const uint8_t *replyMac, uint8_t destId) {
  outgoingMsg.messageType = MSG_TYPE_ROUTE_REPLY;
  outgoingMsg.srcId = NODE_ID;
  outgoingMsg.destId = destId;
  outgoingMsg.origSrcId = NODE_ID;
  outgoingMsg.hopCount = 0;
  outgoingMsg.messageId = nextMessageId++;
  outgoingMsg.payload[0] = '\0';
  
  // Send directly to requesting node
  esp_now_send(replyMac, (uint8_t*)&outgoingMsg, sizeof(outgoingMsg));
}

// Send mesh discovery broadcast
void broadcastDiscovery() {
  M5.Lcd.fillRect(0, 40, 240, 20, BLACK);
  M5.Lcd.setCursor(0, 40);
  M5.Lcd.println("Broadcasting discovery...");
  
  sendMeshMessage(0, MSG_TYPE_DISCOVERY, NODE_NAME, 0);
  lastDiscoveryTime = millis();
}

// Send heartbeat to maintain routes
void sendHeartbeat() {
  sendMeshMessage(0, MSG_TYPE_HEARTBEAT, "", 0);
  lastHeartbeatTime = millis();
}

// Add a message to history to prevent loops
void addMessageToHistory(uint8_t srcId, uint16_t msgId) {
  messageHistory[historyIndex].srcId = srcId;
  messageHistory[historyIndex].messageId = msgId;
  messageHistory[historyIndex].timestamp = millis();
  historyIndex = (historyIndex + 1) % MAX_MESSAGE_HISTORY;
}

// Check if a message is in history
bool isMessageInHistory(uint8_t srcId, uint16_t msgId) {
  for (int i = 0; i < MAX_MESSAGE_HISTORY; i++) {
    if (messageHistory[i].srcId == srcId && 
        messageHistory[i].messageId == msgId &&
        millis() - messageHistory[i].timestamp < 30000) { // 30 second expiry
      return true;
    }
  }
  return false;
}

// Clean up expired routes
void cleanRoutes() {
  for (int i = 0; i < MAX_MESH_PEERS; i++) {
    if (routingTable[i].isActive && 
        millis() - routingTable[i].lastUpdated > ROUTE_EXPIRY_TIME) {
      routingTable[i].isActive = false;
    }
  }
}

// Clean up expired peers
void cleanPeers() {
  for (int i = 0; i < MAX_MESH_PEERS; i++) {
    if (knownPeers[i].lastSeen > 0 && 
        millis() - knownPeers[i].lastSeen > ROUTE_EXPIRY_TIME) {
      knownPeers[i].lastSeen = 0;
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
  M5.Lcd.println("ESP-NOW MESH NETWORK");
  M5.Lcd.printf("NODE ID: %d (%s)\n", NODE_ID, NODE_NAME);
  
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
  
  // Periodic heartbeat (more frequent than discovery)
  if (millis() - lastHeartbeatTime > DISCOVERY_INTERVAL / 3) {
    sendHeartbeat();
  }
  
  // Clean up old routes and peers periodically
  if (millis() % 60000 < 100) {  // Approximately every minute
    cleanRoutes();
    cleanPeers();
  }
  
  // Button A: Send data to a specific node
  if (M5.BtnA.wasPressed()) {
    // Send to node 2 (this can be any node ID in your mesh)
    // In a real application, you might want to cycle through destination nodes
    uint8_t destNode = 2;  // Change this as needed
    char message[64];
    sprintf(message, "Hello from %s", NODE_NAME);
    
    M5.Lcd.fillRect(0, 40, 240, 20, BLACK);
    M5.Lcd.setCursor(0, 40);
    M5.Lcd.printf("Sending to Node %d...\n", destNode);
    
    sendMeshMessage(destNode, MSG_TYPE_DATA, message, analogRead(36) / 4095.0 * 100.0);
  }
  
  // Button B: Trigger discovery broadcast manually
  if (M5.BtnB.wasPressed()) {
    broadcastDiscovery();
  }
  
  delay(50);  // Small delay to prevent hogging the CPU
} 