# WorkerSafety

This project is a wearable IoT-based safety device using M5StickC Plus that automates fall detection and emergency alert systems.

## Hardware and Software Components

### Hardware Components

1. **Microcontrollers:** M5StickC Plus, Arduino Maker Uno
2. **Sensors:** Built-in M5StickC Plus accelerometer, MAX30100 Heart Rate Sensor
3. **Actuators:** Built-in M5StickC Plus buzzer
4. **Communication Devices:** Cytron LoRa RFM Shield

### Software Components

1. **Communication Protocols:** ESP-NOW, MQTT, LoRa
2. **Dashboard:** Flask
3. **IDEs:** Arduino IDe, Visual Studio Code

## Project Structure

```
WorkerSafety
├── lora/                        # Files for LoRa
├── templates/                   # HTML files for dashboard
├── workerA_final/               # Files for M5StickC Plus Worker A
├── workerB_final/               # Files for M5StickC Plus Worker B
├── app.py                       # Site A dashboard
├── centralApp.py                # Central Dashboard
├── README.md                    # Project documentation
└── requirements.txt             # Python dependencies
```

## Setup

### Setup MQTT Broker on Supervisor's laptop

1. On command prompt, change directory to mosquitto
2. Add authentication details. More info (here)[https://mosquitto.org/documentation/authentication-methods/]

```
mosquitto_passwd -c password_file iot_proj
```

```
1234
```

3. Edit mosquitto config file `mosquitto.conf` as Adminstrator

```
listener 1883
allow_anonymous true
password_file password_file
```

4. Run MQTT Broker

```
mosquitto -c mosquitto.conf -v
```

### Setup Dashboards

1. Add a `.env` file to the root directory
2. Setup Amin's username and password in the `.env` file like:
   `USERNAME = admin
   PASSWORD = admin123`

#### Setup Site A Dashboard

1. Change lines 12 & 13 to MQTT Broker's IP address and TX LoRa COM PORT respectively
2. Run `app,py`

```
python app.py
```

#### Setup Central Dashboard

1. Change lines 9 to RX LoRa COM PORT
2. Run `centralApp.py`

```
python centralApp.py
```
