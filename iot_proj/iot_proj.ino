#include <M5StickCPlus.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "MAX30100_PulseOximeter.h"

// Heart rate sensor pins
#define SDA_PIN 32  
#define SCL_PIN 33  

// WiFi and MQTT settings
const char* ssid = "your-internet-ssid"; // wifi hotspot name
const char* password = "your-internet-pass"; // wifi hotspot password
const char* mqtt_server = "raspberry pi ip"; // raspberry pi IP address (run 'hostname -I' on pi terminal or IP Scan)
const int mqtt_port = 1883;
const char* mqtt_user = "raspberry-pi-login-username"; // Your raspberry pi username
const char* mqtt_password = "12345678"; // Your raspberry pi login password if have   

// Worker and floor information. Change workerId and floorId per device
const char* workerID = "worker1";
const char* floorID = "floor1";

// MQTT topics
char topic_fall[50];
char topic_heartrate[50];
char topic_battery[50];
char clientId[50];

WiFiClient espClient;
PubSubClient client(espClient);
PulseOximeter pox;  

// Accelerometer settings
float accX, accY, accZ;
float accMagnitude;
//Not very accurate but might need you guys to help out with tweaking and researching more
float prevMagnitude = 1.0;  // Start with gravity magnitude
float fallThreshold = 0.5;   // Threshold for detecting free-fall
float impactThreshold = 2.0; // Threshold for detecting impact

bool possibleFall = false;  
unsigned long fallDetectionTime = 0;
const int debounceTime = 2000;  // Prevent multiple detections

// Simulated heart rate and battery level
float heartRate;
int batteryLevel = 100; // This should be the actual battery read so that the "supervisor" can know who's device need to be changed/charged

unsigned long lastUpdateTime = 0;
unsigned long lastPublishTime = 0;
unsigned long lastReconnectAttempt = 0;

// Status flags
bool wifiConnected = false;
bool mqttConnected = false;

// Callback function for heart rate sensor
void onBeatDetected() {
    Serial.println("Beat detected!");
}

void setup() {
  M5.begin();
  M5.IMU.Init();
  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.begin(115200);
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);

  // Generate dynamic topics
  sprintf(topic_fall, "/%s/%s/falldetect", floorID, workerID);
  sprintf(topic_heartrate, "/%s/%s/heartrate", floorID, workerID);
  sprintf(topic_battery, "/%s/%s/battery", floorID, workerID);

  // Generate client Id to differentiate all devices 
  sprintf(clientId, "client_%s_%s", floorID, workerID);

  // Connect to WiFi
  connectWiFi();

  // Set up MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Print topics to screen for debugging
  M5.Lcd.println("Topics:");
  M5.Lcd.println(topic_fall);
  M5.Lcd.println(topic_heartrate);
  M5.Lcd.println(topic_battery);
  delay(3000);

  // For heart beat sensor
  if (!pox.begin()) {
        M5.Lcd.setCursor(10, 10);
        M5.Lcd.println("MAX30100 NOT FOUND");
        Serial.println("Sensor initialization failed");
        while (1);
  }

  pox.setOnBeatDetectedCallback(onBeatDetected);
}

void connectWiFi() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Connecting WiFi");

  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    M5.Lcd.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    M5.Lcd.println("\nWiFi connected");
    M5.Lcd.println(WiFi.localIP().toString());
    delay(1000);
  } else {
    M5.Lcd.println("\nWiFi failed!");
    delay(3000);
  }
}

void loop() {
  M5.update();  // Process button inputs
  pox.update();
  
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    if (millis() - lastReconnectAttempt > 10000) {
      lastReconnectAttempt = millis();
      connectWiFi();
    }
  } else {
    wifiConnected = true;
  }

  // Check and maintain MQTT connection
  if (wifiConnected && !client.connected()) {
    mqttConnected = false;
    if (millis() - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = millis();
      reconnect();
    }
  } else if (wifiConnected) {
    mqttConnected = true;
    client.loop();
  }

  // Read accelerometer data
  M5.IMU.getAccelData(&accX, &accY, &accZ);
  
  // Calculate acceleration magnitude 
  accMagnitude = sqrt(accX*accX + accY*accY + accZ*accZ);

  // Fall detection algorithm: Look for free-fall followed by impact
  if (!possibleFall && accMagnitude < fallThreshold) {
    // Possible free-fall detected
    possibleFall = true;
    fallDetectionTime = millis();
  } else if (possibleFall && (millis() - fallDetectionTime < 500)) {
    // Within 500ms of detecting possible free-fall, look for impact
    if (accMagnitude > impactThreshold) {
      // Impact detected after free-fall - FALL DETECTED
      M5.Lcd.fillScreen(RED);
      M5.Lcd.setCursor(0, 0);
      M5.Lcd.println("FALL DETECTED!");
      M5.Lcd.println("Sending alert...");
      
      // Trigger beep
      M5.Beep.beep();  // Start beeping
      delay(1000);     // Beep for 1 second
      M5.Beep.mute();  // Stop beeping

      // Publish fall detection data if connected
      // When detecting a fall
      if (mqttConnected) {
        M5.Lcd.println("Publishing alert...");
        bool success = client.publish(topic_fall, "Fall Detected!");
        if (success) {
          M5.Lcd.println("Alert published!");
        } else {
          M5.Lcd.println("Publish failed!");
        }
      }
      
      possibleFall = false;
      delay(debounceTime);  // Prevent multiple triggers
    }
  } else if (possibleFall && (millis() - fallDetectionTime >= 500)) {
    // Reset if no impact detected within timeframe
    possibleFall = false;
  }
  
  // Update display every 1 second
  if (millis() - lastUpdateTime > 1000) {
    lastUpdateTime = millis();
    updateDisplay();
  }
  
  // Publish heart rate and battery every 5 seconds
  if (millis() - lastPublishTime > 5000) {
    lastPublishTime = millis();

    // For heart rate sensor - need to keep here if not, wont work
    heartRate = pox.getHeartRate();  // Update heart rate value
    Serial.printf("BPM: %0.1f\n", heartRate);

    updateSensors();
    publishData();
  }
  
  delay(10);  // Small delay for stability
}

void updateDisplay() {
  if (!possibleFall) {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("Fall Detector");
    
    M5.Lcd.printf("Acc: %.2f\n", accMagnitude);
    M5.Lcd.printf("HR: %.1f BPM\n", heartRate);
    M5.Lcd.printf("Batt: %d%%\n", batteryLevel);
    
    // Display connection status
    M5.Lcd.setCursor(0, 100);
    M5.Lcd.printf("WiFi: %s\n", wifiConnected ? "OK" : "NO");
    M5.Lcd.printf("MQTT: %s\n", mqttConnected ? "OK" : "NO");
  }
}

void updateSensors() {
  // Simulate battery drain
  batteryLevel -= random(0, 2);
  if (batteryLevel < 0) batteryLevel = 100;
}

void publishData() {
  if (mqttConnected) {
    // Publish heart rate
    char heartRateMsg[10];
    sprintf(heartRateMsg, "%.1f", heartRate);
    client.publish(topic_heartrate, heartRateMsg);
    
    // Publish battery level
    char batteryMsg[10];
    sprintf(batteryMsg, "%d", batteryLevel);
    client.publish(topic_battery, batteryMsg);
  }
}

void reconnect() {
  if (client.connect(clientId, mqtt_user, mqtt_password)) {
    mqttConnected = true;
    // Resubscribe if needed
    client.subscribe(topic_fall);
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
}