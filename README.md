# Worker Safety System

A complete safety monitoring solution for workers, featuring fall detection, heart rate monitoring, and emergency alerting capabilities through both direct device-to-device communication and centralized monitoring.

## System Overview

The Worker Safety System consists of:

1. **Worker A Device (M5StickC Plus)**: Worn by the worker, monitors for falls and vital signs
2. **Worker B Device (M5StickC Plus)**: Carried by a supervisor or safety officer, receives emergency alerts
3. **MQTT Broker & Dashboard**: Central monitoring system running on a Raspberry Pi

The system features dual communication paths:
- **ESP-NOW**: Direct device-to-device communication for immediate emergency response
- **MQTT**: Cloud connectivity for remote monitoring, data logging, and analytics

## Hardware Requirements

### For Each Worker Device (2 sets needed)

- [M5StickC Plus](https://shop.m5stack.com/products/m5stickc-plus-esp32-pico-mini-iot-development-kit) - Compact ESP32 development kit with display, IMU, and battery
- [MAX30100 Pulse Oximeter](https://www.sparkfun.com/products/15219) (optional) - For heart rate monitoring
- USB-C cable for programming and charging

### For Central Monitoring

- Raspberry Pi (3B+ or better recommended)
- Power supply for Raspberry Pi
- MicroSD card (16GB+ recommended)
- WiFi connectivity or Ethernet connection

## Software Prerequisites

### For M5StickC Plus Devices

1. [Arduino IDE](https://www.arduino.cc/en/software) (1.8.13 or newer)
2. Required Libraries:
   - M5StickCPlus (by M5Stack)
   - ESP-NOW (included in ESP32 board package)
   - PubSubClient (for MQTT)
   - MAX30100_PulseOximeter (if using heart rate sensor)

### For Raspberry Pi

1. Mosquitto MQTT broker
2. Python 3.6+
3. Paho MQTT client library for Python

## Installation Instructions

### 1. Arduino IDE Setup

1. Install Arduino IDE from [arduino.cc](https://www.arduino.cc/en/software)
2. Add ESP32 board support:
   - Open Arduino IDE
   - Go to File > Preferences
   - Add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` to Additional Boards Manager URLs
   - Go to Tools > Board > Boards Manager...
   - Search for "esp32" and install the ESP32 package

3. Install required libraries:
   - Go to Tools > Manage Libraries...
   - Search for and install:
     - "M5StickCPlus" by M5Stack
     - "PubSubClient" by Nick O'Leary
     - "MAX30100lib" by OXullo Intersecans (if using heart rate sensor)

### 2. Raspberry Pi MQTT Broker Setup

1. Install Mosquitto MQTT broker:
   ```bash
   sudo apt update
   sudo apt install -y mosquitto mosquitto-clients
   sudo systemctl enable mosquitto.service
   ```

2. Configure Mosquitto for remote connections:
   ```bash
   sudo nano /etc/mosquitto/mosquitto.conf
   ```
   Add the following lines:
   ```
   listener 1883
   allow_anonymous false
   password_file /etc/mosquitto/passwd
   ```

3. Create a user account:
   ```bash
   sudo mosquitto_passwd -c /etc/mosquitto/passwd motherpi
   ```
   Enter the password when prompted (e.g., "12345678")

4. Restart Mosquitto:
   ```bash
   sudo systemctl restart mosquitto
   ```

5. Install Python MQTT client:
   ```bash
   sudo apt install -y python3-pip
   pip3 install paho-mqtt
   ```

6. Copy the `mqtt_subscriber.py` script to your Raspberry Pi

### 3. Hardware Assembly

1. **Heart Rate Sensor Connection (optional):**
   - Connect SDA of MAX30100 to pin 32 of M5StickC Plus
   - Connect SCL of MAX30100 to pin 33 of M5StickC Plus
   - Connect VIN to 3.3V on M5StickC Plus
   - Connect GND to GND on M5StickC Plus

2. **Charge both M5StickC Plus units** before deploying

## Configuration

### 1. Worker A Device (M5StickC Plus with Fall Detection)

1. Get the MAC address of Worker B's device (it will be displayed on Worker B's screen during startup)
2. Open `workerA.ino` in Arduino IDE
3. Update the following variables:
   ```c
   // WiFi and MQTT settings
   const char* ssid = "your-wifi-ssid";     // Replace with your WiFi SSID
   const char* password = "your-wifi-pass"; // Replace with your WiFi password
   const char* mqtt_server = "192.168.x.x"; // Replace with your Raspberry Pi's IP address
   const char* mqtt_user = "motherpi";      // MQTT username
   const char* mqtt_password = "12345678";  // MQTT password

   // Worker and floor information
   const char* workerID = "worker1";        // Change per device
   const char* floorID = "floor1";          // Change per location

   // MAC Address of Worker B's device
   uint8_t workerBMacAddress[] = {0x4C, 0x75, 0x25, 0xCB, 0x90, 0x88};  // Replace with Worker B's MAC
   ```

4. Adjust fall detection sensitivity if needed:
   ```c
   float impactThreshold = 3.0; // Threshold for detecting a fall
   ```

5. Connect M5StickC Plus to your computer and select:
   - Tools > Board > ESP32 Arduino > M5Stick-C
   - Select the correct COM port
   - Upload the sketch

### 2. Worker B Device (M5StickC Plus for Emergency Response)

1. Get the MAC address of Worker A's device (it will be displayed on Worker A's screen during startup)
2. Open `workerB.ino` in Arduino IDE
3. Update the MAC address of Worker A:
   ```c
   uint8_t workerAMacAddress[] = {0x0C, 0x8B, 0x95, 0xA8, 0x15, 0x78};  // Replace with Worker A's MAC
   ```

4. Connect M5StickC Plus to your computer and select:
   - Tools > Board > ESP32 Arduino > M5Stick-C
   - Select the correct COM port
   - Upload the sketch

### 3. MQTT Monitoring (Raspberry Pi)

1. Configure your Raspberry Pi to have a static IP address
2. Start the MQTT subscriber:
   ```bash
   python3 mqtt_subscriber.py
   ```

## Usage

### Worker A Device (User Wearing the Device)

- The device continuously monitors for falls using the accelerometer
- Heart rate is monitored if the MAX30100 sensor is connected
- Battery level is monitored and reported
- Button A: Press to acknowledge recovery from a fall
- Button B: Press to send a manual emergency alert

### Worker B Device (Supervisor/Emergency Responder)

- Receives fall alert notifications from Worker A
- Displays vital information during emergencies (impact force, heart rate)
- Sounds an alarm during emergencies
- Button A: Press to acknowledge an emergency alert
- Button B: Press to send a manual status update

### Raspberry Pi Dashboard

- Monitors all worker devices on the network
- Logs all fall detection events, heart rate data, and battery levels
- Can be extended with a web interface for historical data visualization

## Troubleshooting

### ESP-NOW Connection Issues

- Ensure both devices are within range (approximately 30-50 meters line of sight)
- Verify the MAC addresses are entered correctly in the code
- Try resetting both devices

### MQTT Connection Issues

- Verify WiFi credentials are correct
- Check that the Raspberry Pi IP address is correct
- Ensure Mosquitto is running on the Raspberry Pi:
  ```bash
  sudo systemctl status mosquitto
  ```
- Test MQTT broker with command line tools:
  ```bash
  mosquitto_sub -h localhost -t "/#" -u motherpi -P 12345678
  ```

### Heart Rate Sensor Issues

- Check wiring connections
- Ensure the sensor is properly placed on the skin for readings
- Try adjusting the position of the sensor

## Advanced Customization

### Adjusting Fall Detection Sensitivity

If you experience false alarms or missed falls, adjust these parameters:
```c
float impactThreshold = 3.0; // Increase for less sensitivity, decrease for more
const int debounceTime = 2000; // Time in ms to prevent multiple detections
```

### Battery Life Optimization

To increase battery life:
```c
const int heartRateCheckInterval = 1000; // Increase to check heart rate less frequently
const int batteryCheckInterval = 30000; // Increase to check battery less frequently
```

### Enhancing Security

For better security:
- Change default passwords
- Consider enabling MQTT over TLS
- Implement message payload encryption

## License

This project is released under the MIT License.

## Credits

Developed for worker safety applications using the M5StickC Plus platform.
