# SITE A SETUP
# Change line 13 to MQTT broker IP
# Change line 12 to COM port connected to TX LoRa

from flask import Flask, render_template, jsonify, request, redirect, url_for, flash, session, after_this_request
import paho.mqtt.client as mqtt
import threading
import serial
from flask_login import LoginManager, UserMixin, login_user, logout_user, login_required, current_user
import os
import dotenv

app = Flask(__name__)
app.secret_key = os.urandom(24)
app.config.update(
    SESSION_COOKIE_SECURE=False,  # False for localhost
    SESSION_COOKIE_HTTPONLY=True,  # Prevent JavaScript access to session cookie
    SESSION_COOKIE_SAMESITE='Lax',  # Restrict cookie sharing
    PERMANENT_SESSION_LIFETIME=1800  # Session timeout in seconds (30 minutes)
)
PORT = 'COM4'                       # Change to the COM port connected to TX LoRa
BROKER_ADD = "192.168.0.111"        # Change
ser = None

# Setup Flask-Login
login_manager = LoginManager()
login_manager.init_app(app)
login_manager.login_view = 'login'

class User(UserMixin):
    def __init__(self, id, username, password):
        self.id = id
        self.username = username
        self.password = password

username = dotenv.get_key('.env', 'USERNAME')
password = dotenv.get_key('.env', 'PASSWORD')

users = {
    'admin': User(1, username, password)
}

@login_manager.user_loader
def load_user(user_id):
    for user in users.values():
        if user.id == int(user_id):
            return user
    return None

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

@app.after_request
def add_no_cache_headers(response):
    """Add headers to prevent browser caching."""
    response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate, max-age=0"
    response.headers["Pragma"] = "no-cache"
    response.headers["Expires"] = "0"
    return response

# Callback when the client connects to the broker
def on_connect(client, userdata, flags, rc):
    print(f"Connected with result code {rc}")
    if rc == 0:
        print("Successfully connected to MQTT broker")
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
client.username_pw_set("iot_proj", "1234")

print("Connecting to MQTT broker...")
# Connect to the broker (change IP)
client.connect(BROKER_ADD, 1883, 60)

# Start the MQTT client loop in a background thread
mqtt_thread = threading.Thread(target=mqtt_loop, daemon=True)
mqtt_thread.start()

# Read and print COM port to log LoRa messages
def serial_reader():
    global ser
    if ser is None:
        print("Serial port not available.")
        return

    print("Started serial reader thread...")
    while True:
        try:
            line = ser.readline().decode('utf-8').strip()
            if line:
                print(f"Serial: {line}")
        except Exception as e:
            print(f"Error reading from serial: {e}")
            break

@app.route('/api/check-session')
def check_session():
    if current_user.is_authenticated:
        return jsonify({"status": "authenticated"})
    return jsonify({"status": "unauthenticated"}), 401

@app.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        username = request.form.get('username')
        password = request.form.get('password')
                
        if username in users and users[username].password == password:
            try:
                login_user(users[username])
                next_page = request.args.get('next')
                
                # Debug print
                print(f"Login successful for {username}, redirecting to: {next_page or '/'}")
                
                return redirect(next_page or url_for('main'))
            except Exception as e:
                print(f"Error during login: {e}")
                flash(f"An error occurred during login: {e}")
        else:
            flash('Invalid username or password')
    
    return render_template('login.html')

@app.route('/logout')
@login_required
def logout():
    # Clear the session
    logout_user()
    session.clear()
    
    response = redirect(url_for('login'))
    response.set_cookie('session', '', expires=0)
    flash('You have been logged out successfully')
    
    return response

@app.route("/")
@login_required
def main():
    return render_template("dashboard.html", data=data_store)

# API endpoint to get lastest data
@app.route("/api/data")
@login_required
def get_data():
    return jsonify(data_store)

if __name__ == "__main__":
    try:
        ser = serial.Serial(PORT, 9600, timeout=1)
        print(f"Serial port {PORT} opened successfully")    
    except Exception as e:
        print(f"Failed to open serial port: {e}")
        ser = None

    serial_thread = threading.Thread(target=serial_reader, daemon=True)
    serial_thread.start()
        
    app.run(host="localhost", debug=False)