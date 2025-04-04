# CENTRAL DASHBOARD SETUP
# Change line 84 to COM port connected to RX LoRa

from flask import Flask, render_template, jsonify
import threading
import serial
import time

app = Flask(__name__)
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

@app.route("/")
def main():
    print(data_store)
    return render_template("centralDashboard.html", data=data_store)

# API endpoint to get lastest data
@app.route("/api/data")
def get_data():
    return jsonify(data_store)

# Handle message from LoRa
def handle_message(message):
    try:
        if "From " in message and ":" in message:
            before_site, _, after_site = message.partition("From ")
            site_info, _, topic_payload = after_site.partition(":")
            
            site_info = site_info.strip()
            topic_payload = topic_payload.strip()

            topic_parts = topic_payload.strip("/").split("/")

            if len(topic_parts) == 4:
                floor_id = topic_parts[0]
                worker_id = topic_parts[1]
                sensor_type = topic_parts[2]
                sensor_value = topic_parts[3]

                # Initialize site
                if site_info not in data_store:
                    data_store[site_info] = {}
                if floor_id not in data_store[site_info]:
                    data_store[site_info][floor_id] = {}
                if worker_id not in data_store[site_info][floor_id]:
                    data_store[site_info][floor_id][worker_id] = {}

                # Store value
                data_store[site_info][floor_id][worker_id][sensor_type] = sensor_value
                print(f"[INFO] Updated data_store: {data_store}")
            else:
                print(f"[ERROR] Invalid topic format: {topic_parts}")
        else:
            print("[ERROR] Message format doesn't match expected pattern")
    except Exception as e:
        print(f"[EXCEPTION] in handle_message: {e}")

def read_ser6():
    global ser_com6
    while True:
        if ser_com6 is not None and ser_com6.in_waiting > 0:
            message = ser_com6.readline().decode('utf-8').strip()
            
            # Filter to only print the specific message
            if "Got valid message:" in message:
                print(f"Received from COM6: {message}")
                handle_message(message)

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
    ser6_thread()
    app.run(host="0.0.0.0", port=5001, debug=False)