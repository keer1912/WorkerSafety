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
├── esp_now_nodeA/               # Files for ESP-NOW Node A
├── esp_now_nodeB/               # Files for ESP-NOW Node B
├── esp_now_receiver/            # Files for ESP-NOW Receiver
├── esp_now_receiver/            # Files for ESP-NOW Sender
├── esp_now_receiver/            # Files for ESP-NOW Sender
├── fall_detector/               # Fall detector codes
├── lora/                        # Files for LoRa
├── templates/                   # HTML files for dashboard
├── app.py                       # Site A dashboard
├── centralApp.py                # Central Dashboard
├── mqtt_sibscriber.py           # MQTT script
├── README.md                    # Project documentation
└── requirements.txt             # Python dependencies
```

## Fall Detector
Using the M5STICKCPlus this program detects falls using its inbuilt accelerometer. Then the data is sent over to a pi which is set up as the supervisor's machine on a construction worksite to know which worker has fell down.

When a fall is detected, the device will sound out a beep from its buzzer as well.

Here is a diagram that depicts the rough idea for the set-up:\
![image](https://github.com/user-attachments/assets/7723535f-9781-433c-a781-9c0074211dd4)

Optionally, bluetooth beacons can be implemented to find the location of the worker on site to give medical attention

## Setup for fall detector code
1.  Configure the WiFi and MQTT settings in the code:
    
    -   Set your WiFi SSID and password
        
    -   Set the MQTT broker IP address (your Raspberry Pi IP) 
	    - To find RPI IP you can run `hostname -I` on the pi terminal or IP Scan.
        
    -   Configure MQTT username and password if applicable

## Set up MQTT Broker for Worker to Supervisor System on RPI
### Features
-   Connects to an MQTT broker
    
-   Subscribes to topics for fall detection, heart rate, and battery level
    
-   Processes incoming messages and prints relevant information
    
-   Handles multiple workers and floors
    
###  Requirements
-   Python 3.x
-   Flask
-   paho-mqtt library
    
###  Installation

1. Ensure Python 3.x is installed on your Pi.
2. Create your own Python environment `python -m venv name-of-your-env` and activate it `source name-of-your-env/bin/activate`. For more info, visit https://docs.python.org/3/tutorial/venv.html.
4. Install required libraries:
  `pip install -r requirements.txt`
5. Add folder `/templates` and file `app.py` to your Pi
6. Run using the command
  `python app.py`

### Output on RPI
-   Connection status to the MQTT broker\
   ![image](https://github.com/user-attachments/assets/6a1b83b9-8bac-40e9-949e-c9dff233a0e8)

-   Dashboard URL\
    ![image](https://github.com/user-attachments/assets/f6e52951-f2af-4a40-a914-437866315160) \
  	- To view the dashboard on other devices like a laptop, use the link `http://<Pi's IP>:5000`, from the image example, the link is `http://192.168.86.220:5000`

-   Subscription confirmations
    
-   Received messages with their topics and payloads
    
-   Processed information about falls, heart rates, and battery levels

In the context of our fall detection, the topics follow this format:

`/<floor_id>/<worker_id>/<sensor_type>`

### Things to take note
1. The M5StickC Plus acts as an MQTT client, publishing data to topics on the broker which is the RPI.

2. The M5StickC Plus publishes data to topics like\
`"/<floor_id>/<worker_id>/falldetect", "/<floor_id>/<worker_id>/heartrate", and "/<floor_id>/<worker_id>/battery"`

4. Example output on RPI\
![image](https://github.com/user-attachments/assets/8f2f2795-6a76-4133-9160-74e21c1d4bba)
