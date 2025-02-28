import paho.mqtt.client as mqtt
import time

# Callback when the client connects to the broker
def on_connect(client, userdata, flags, rc):
    print(f"Connected with result code {rc}")
    if rc == 0:
        print("Successfully connected to MQTT broker")
        # Subscribe to all workers' data on all floors
        client.subscribe("/#")  # Subscribe to all topics for testing
        print("Subscribed to all topics")

        # Subscribe to specific topics
        topics = ["/+/+/falldetect", "/+/+/heartrate", "/+/+/battery"]
        for topic in topics:
            client.subscribe(topic)
            print(f"Subscribed to {topic}")
    else:
        print(f"Failed to connect, return code {rc}")

# Callback when a message is received
def on_message(client, userdata, msg):
    print(f"RECEIVED: {msg.topic} = {msg.payload.decode()}")

    # Extract floor and worker ID from the topic
    topic_parts = msg.topic.split("/")
    if len(topic_parts) >= 4:  # Make sure we have enough parts
        floor_id = topic_parts[1]
        worker_id = topic_parts[2]
        sensor_type = topic_parts[3]

        # Process the message based on the sensor type
        if sensor_type == "falldetect":
            print(f"ALERT! Worker {worker_id} on floor {floor_id} has fallen!")
        elif sensor_type == "heartrate":
            print(f"Worker {worker_id} on floor {floor_id} has heart rate: {msg.payload.decode()} bpm")
        elif sensor_type == "battery":
            print(f"Worker {worker_id} on floor {floor_id} has battery level: {msg.payload.decode()}%")

# Create MQTT client
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

# Set authentication credentials
client.username_pw_set("motherpi", "12345678")

print("Connecting to MQTT broker...")
# Connect to the broker
client.connect("localhost", 1883, 60)

# Start the loop to process incoming messages
print("Starting message loop - press Ctrl+C to exit")
try:
    client.loop_forever()
except KeyboardInterrupt:
    print("Exiting")
    client.disconnect()
