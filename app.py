from flask import Flask, render_template, jsonify
import paho.mqtt.client as mqtt
import threading
import serial
import time

app = Flask(__name__)
ser = None
ser6 = None

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
        topics = ["/+/+/falldetect", "/+/+/heartrate", "/+/+/battery"]
        for topic in topics:
            client.subscribe(topic)
            print(f"Subscribed to {topic}")
    else:
        print(f"Failed to connect, return code {rc}")

# Callback when a message is received
def on_message(client, userdata, msg):
    global ser
    topic_parts = msg.topic.split("/")
    
    try:
        message = msg.payload.decode('utf-8')
        topic = msg.topic  # e.g., "/floor1/worker23/heartrate/75"

        print(f"📥 Received Message: Topic = {topic}, Payload = {message}")  # PRINT RECEIVED DATA

        payload = topic + "/" + message
        print("Payload: " + payload)
        
        if ser is not None:
            ser.write((payload + "\n").encode())  # Send raw topic + newline
            print(f"Sent to serial: {payload}")
    except Exception as e:
        print(f"Error: {e}")
    
    if len(topic_parts) >= 4:
        floor_id = topic_parts[1]  # Correct extraction from topic
        worker_id = topic_parts[2]  # Correct extraction from topic
        sensor_type = topic_parts[3]
        
        # Initialize nested dictionaries if they don't exist
        if floor_id not in data_store:
            data_store[floor_id] = {}
        if worker_id not in data_store[floor_id]:
            data_store[floor_id][worker_id] = {}
            
        # Store the data
        data_store[floor_id][worker_id][sensor_type] = message

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
# client.username_pw_set("username", "passwd")

print("Connecting to MQTT broker...")
# Connect to the broker (change IP)
client.connect("172.20.10.6", 1883, 60)

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

# def read_ser6():
#     global ser_com6
#     while True:
#         if ser_com6 is not None and ser_com6.in_waiting > 0:
#             message = ser_com6.readline().decode('utf-8').strip()
#             print(f"Received from COM6: {message}")
            
#             # # Extract and store data from COM6 message
#             # # Assuming format: "Got valid message: From SITE_A: /floor1/worker1/heartrate/60"
#             # if "Got valid message:" in message:
#             #     parts = message.split(": ")
#             #     if len(parts) == 2:
#             #         print(f"Message received from COM6: {parts[1]}")
#             #         # Here, add logic to extract and store the data as needed (like updating `data_store`)
#             #         # Example: Update data_store based on the received message format

def read_ser6():
    global ser_com6
    while True:
        if ser_com6 is not None and ser_com6.in_waiting > 0:
            message = ser_com6.readline().decode('utf-8').strip()
            
            # Filter to only print the specific message
            if "Got valid message:" in message:
                print(f"Received from COM6: {message}")


def ser6_thread():
    try:
        global ser_com6
        ser_com6 = serial.Serial('COM6', 9600, timeout=1)
        print("Serial port COM6 opened successfully")

        serial_thread = threading.Thread(target=read_ser6, daemon=True)
        serial_thread.start()

    except Exception as e:
        print(f"Failed to open serial port COM6: {e}")
        ser_com6 = None

if __name__ == "__main__":
    try:
        ser = serial.Serial('COM4', 9600, timeout=1)
        print("Serial port COM4 opened successfully")    
    except Exception as e:
        print(f"Failed to open serial port: {e}")
        ser = None
        ser6 = None

    ser6_thread()
        
    app.run(host="0.0.0.0", debug=False)