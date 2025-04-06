# CENTRAL DASHBOARD SETUP
# Change line 9 to COM port connected to RX LoRa

from flask import Flask, render_template, jsonify, request, redirect, url_for, flash, session, after_this_request
from flask_login import LoginManager, UserMixin, login_user, logout_user, login_required, current_user
import os
import dotenv
import threading
import serial

PORT = 'COM3'
app = Flask(__name__)
app.secret_key = os.urandom(24)
app.config.update(
    SESSION_COOKIE_SECURE=False,  # False for localhost
    SESSION_COOKIE_HTTPONLY=True,  # Prevent JavaScript access to session cookie
    SESSION_COOKIE_SAMESITE='Lax',  # Restrict cookie sharing
    PERMANENT_SESSION_LIFETIME=1800  # Session timeout in seconds (30 minutes)
)
ser6 = None

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

@app.after_request
def add_no_cache_headers(response):
    """Add headers to prevent browser caching."""
    response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate, max-age=0"
    response.headers["Pragma"] = "no-cache"
    response.headers["Expires"] = "0"
    return response

data_store = {
    # Site ID → Floor ID → Worker ID → Sensor type → Value
    # Example data
    # "SITE_A": {
    #     "floor1": {
    #         "worker1": {
    #             "falldetect": "0",
    #             "heartrate": "75",
    #             "battery": "90"
    #         },
    #         "worker2": {
    #             "falldetect": "0",
    #             "heartrate": "60",
    #             "battery": "80"
    #         }
    #     }
    # }
}

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
    print(data_store)
    return render_template("centralDashboard.html", data=data_store)

# API endpoint to get lastest data
@app.route("/api/data")
@login_required
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
            try:
                # Decode with error handling
                message = ser_com6.readline().decode('utf-8', errors='replace').strip()
                
                # Filter to only print the specific message
                if "Got valid message:" in message:
                    print(f"Received from {PORT}: {message}")
                    handle_message(message)
            except Exception as e:
                print(f"[EXCEPTION] in read_ser6: {e}")
                
def ser6_thread():
    try:
        global ser_com6
        ser_com6 = serial.Serial(PORT, 9600, timeout=1)
        print(f"Serial port {PORT} opened successfully")

        serial_thread = threading.Thread(target=read_ser6, daemon=True)
        serial_thread.start()

    except Exception as e:
        print(f"Failed to open serial port COM6: {e}")
        ser_com6 = None

if __name__ == "__main__":
    ser6_thread()
    app.run(host="localhost", port=5001, debug=False)