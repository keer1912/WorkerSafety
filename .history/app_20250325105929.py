from flask import Flask, render_template, jsonify
import paho.mqtt.client as mqtt
import threading

app = Flask(__name__)

data_store = {
    # Floor ID → Worker ID → Sensor type → Value
    # Example data
    # "1": {
    #     "worker1": {
    #         "falldetect": "0",
    #         "heartrate": "75",
    #         "battery": "90"
    #     }
    # }
}

# Callback when the client connects to the broker
def on_connect(client, userdata, flags, rc):
    print(f"Connected with result code {rc}")
    if rc == 0:
        print("Successfully connected to MQTT broker")
        # Subscribe to all workers' data on all floors
        # client.subscribe("/#")  # Subscribe to all topics for testing
        # print("Subscribed to all topics")

        # Subscribe to specific topics
        topics = ["/A/+/+/falldetect", "/A/+/+/heartrate", "/A/+/+/battery",
                  "/B/+/+/falldetect", "/B/+/+/heartrate", "/B/+/+/battery",
                  "/C/+/+/falldetect", "/C/+/+/heartrate", "/C/+/+/battery",
                  "/D/+/+/falldetect", "/D/+/+/heartrate", "/D/+/+/battery"]
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
        value = msg.payload.decode()
        
        # Initialize nested dictionaries if they don't exist
        if floor_id not in data_store:
            data_store[floor_id] = {}
        if worker_id not in data_store[floor_id]:
            data_store[floor_id][worker_id] = {}
            
        # Store the data
        data_store[floor_id][worker_id][sensor_type] = value

        # Process the message based on the sensor type
        if sensor_type == "falldetect":
            print(f"ALERT! Worker {worker_id} on floor {floor_id} has fallen!")
        elif sensor_type == "heartrate":
            print(f"Worker {worker_id} on floor {floor_id} has heart rate: {msg.payload.decode()} bpm")
        elif sensor_type == "battery":
            print(f"Worker {worker_id} on floor {floor_id} has battery level: {msg.payload.decode()}%")

def mqtt_loop():
    try:
        print("Starting MQTT loop...")
        client.loop_forever()
    except KeyboardInterrupt:
        print("Exiting")
        client.disconnect()

# Create MQTT client
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

# Set authentication credentials
client.username_pw_set("username", "passwd")

print("Connecting to MQTT broker...")
# Connect to the broker
client.connect("localhost", 1883, 60)

# Start the MQTT client loop in a background thread
mqtt_thread = threading.Thread(target=mqtt_loop, daemon=True)
mqtt_thread.start()

@app.route("/")
def main():
    return render_template("dashboard.html", data=data_store)

# API endpoint to get lastest data
@app.route("/api/data")
def get_data():
    return jsonify(data_store)

if __name__ == "__main__":
    app.run(host='0.0.0.0', port=5000, debug=True)