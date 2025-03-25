# ESP-NOW Mesh Network for M5StickC Plus

This project implements a mesh network using ESP-NOW protocol on M5StickC Plus devices. It allows for multi-hop communication between nodes, enabling extended range and resilient communication paths.

## Features

- Auto-discovery of mesh nodes
- Multi-hop message routing
- Automatic route discovery and maintenance
- Message deduplication to prevent loops
- Heartbeat mechanism to maintain network topology
- Support for both directed and broadcast messages
- Visual feedback on the M5StickC Plus display

## Getting Started

### Prerequisites

- Multiple M5StickC Plus devices
- Arduino IDE with ESP32 support
- M5StickC Plus library installed

### Setup Instructions

1. **Assign unique IDs to each node**:
   - Open each `.ino` file and modify the `NODE_ID` and `NODE_NAME` constants at the top
   - Each node must have a unique ID (1, 2, 3, etc.)

2. **Flash the code**:
   - Flash `esp_now_mesh_node.ino` to your first M5StickC Plus (Node 1)
   - Flash `esp_now_mesh_node2.ino` to your second M5StickC Plus (Node 2)
   - For additional nodes, copy one of the files, change the NODE_ID and NODE_NAME, and flash to the device

3. **Power up the nodes**:
   - Power up all devices
   - They will automatically start sending discovery broadcasts and forming the mesh network

## Using the Mesh Network

### Button Controls

- **Button A**: Send a test message to another node (default: Node 1 sends to Node 2, Node 2 sends to Node 1)
- **Button B**: Manually trigger a discovery broadcast to rebuild the network topology

### Display Information

The display shows:
- Node ID and name
- MAC address of the device
- Status of message sending
- Received messages with the sender node ID
- Discovery and route finding status

### Adding More Nodes

To add more nodes to the network:
1. Copy one of the existing `.ino` files
2. Change the `NODE_ID` (e.g., to 3, 4, etc.) and `NODE_NAME`
3. Optionally modify the default destination node in the Button A handler
4. Flash to a new M5StickC Plus device

## How It Works

### Network Formation

1. Each node broadcasts discovery messages periodically
2. When a node receives a discovery message, it:
   - Updates its routing table
   - Forwards the discovery to other nodes (up to a maximum hop count)
   - Adds the sender to its list of known peers

### Message Routing

1. To send a message to a specific node:
   - The node checks its routing table for the destination
   - If a route exists, it sends the message to the next hop
   - If no route exists, it initiates route discovery

2. Route discovery process:
   - The node broadcasts a route request message
   - Intermediate nodes forward the request until it reaches the destination
   - The destination node sends a route reply back
   - The route reply is forwarded back to the originator
   - Each node updates its routing table with the new route information

### Preventing Loops

The mesh network uses several mechanisms to prevent message loops:
- Each message has a unique ID and source node ID
- Nodes track recently seen messages
- Messages have a maximum hop count
- Messages are not forwarded back to their source

## Customization

You can customize these parameters in the code:
- `MAX_HOP_COUNT`: Maximum number of hops a message can travel (default: 5)
- `DISCOVERY_INTERVAL`: Time between auto-discovery broadcasts (default: 30 seconds)
- `ROUTE_EXPIRY_TIME`: Time before routes expire (default: 5 minutes)
- `MAX_MESH_PEERS`: Maximum number of peers a node can track (default: 20)

## Troubleshooting

- **Nodes aren't discovering each other**: Make sure they're within range or have intermediate nodes to relay
- **Messages not reaching destination**: Check that the destination node ID exists in the network
- **Display shows "Send FAILED"**: The next hop node may be out of range or powered off

## Security Considerations

This implementation does not include encryption. For a secure mesh network, you would need to:
1. Implement message encryption
2. Add authentication for mesh nodes
3. Consider using the key rotation mechanism from your existing code 