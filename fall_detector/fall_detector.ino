#include <M5StickCPlus.h>
#include <WiFi.h>
#include <PubSubClient.h>

// WiFi and MQTT settings (UPDATE THESE!)
const char* ssid = "Xiaomi Nya";          // Your Wi-Fi name
const char* password = "itamenekos";      // Your Wi-Fi password
const char* mqtt_server = "192.168.60.27"; // Your laptop's IP (run `ipconfig` on Windows or `ifconfig` on Mac/Linux)
const int mqtt_port = 1883;               // Default MQTT port
const char* mqtt_user = "";               // Leave empty if no auth
const char* mqtt_password = "";           // Leave empty if no auth

// Worker and floor information
const char* workerID = "worker1";
const char* floorID = "floor1";

// MQTT topics
char topic_fall[50];
char topic_heartrate[50];
char topic_battery[50];
char topic_recovery[50];

WiFiClient espClient;
PubSubClient client(espClient);

// Accelerometer data
float accX, accY, accZ;
float accelMagnitude;
float impactThreshold = 3.0; // Threshold for detecting a fall

// State machine
enum UserState {STANDING, FALLEN};
UserState userState = STANDING;

bool possibleFall = false;
unsigned long fallDetectionTime = 0;
const int debounceTime = 2000;  // Prevent multiple detections

// Simulated heart rate and battery level
int heartRate = 75;
int batteryLevel = 100;

unsigned long lastUpdateTime = 0;
unsigned long lastPublishTime = 0;
unsigned long lastReconnectAttempt = 0;

// Status flags
bool wifiConnected = false;
bool mqttConnected = false;

// Moving average for acceleration smoothing
const int movingAverageWindow = 10;
float accelMagnitudeHistory[movingAverageWindow];
int accelMagnitudeIndex = 0;
float accelMagnitudeAverage = 0;

void setup() {
  M5.begin();
  M5.IMU.Init();
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);

  // Generate MQTT topics
  sprintf(topic_fall, "/%s/%s/falldetect", floorID, workerID);
  sprintf(topic_heartrate, "/%s/%s/heartrate", floorID, workerID);
  sprintf(topic_battery, "/%s/%s/battery", floorID, workerID);
  sprintf(topic_recovery, "/%s/%s/recovery", floorID, workerID);

  // Connect to WiFi
  connectWiFi();

  // Set up MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Print topics for debugging
  M5.Lcd.println("Topics:");
  M5.Lcd.println(topic_fall);
  M5.Lcd.println(topic_heartrate);
  M5.Lcd.println(topic_battery);
  M5.Lcd.println(topic_recovery);
  delay(3000);

  // Initialize moving average
  for (int i = 0; i < movingAverageWindow; i++) {
    accelMagnitudeHistory[i] = 1.0; // Start with gravity (1G)
  }
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
  M5.update();  // Check button presses

  // Reconnect WiFi if lost
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    if (millis() - lastReconnectAttempt > 10000) {
      lastReconnectAttempt = millis();
      connectWiFi();
    }
  } else {
    wifiConnected = true;
  }

  // Reconnect MQTT if lost
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

  // Read accelerometer
  M5.IMU.getAccelData(&accX, &accY, &accZ);
  accelMagnitude = sqrt(accX*accX + accY*accY + accZ*accZ);

  // Update moving average
  accelMagnitudeHistory[accelMagnitudeIndex] = accelMagnitude;
  accelMagnitudeIndex = (accelMagnitudeIndex + 1) % movingAverageWindow;
  accelMagnitudeAverage = 0;
  for (int i = 0; i < movingAverageWindow; i++) {
    accelMagnitudeAverage += accelMagnitudeHistory[i];
  }
  accelMagnitudeAverage /= movingAverageWindow;

  // Fall detection logic
  fall_detection();
  
  // Update display every 1 second
  if (millis() - lastUpdateTime > 1000) {
    lastUpdateTime = millis();
    updateDisplay();
  }
  
  // Publish sensor data every 1 second
  if (millis() - lastPublishTime > 1000) {
    lastPublishTime = millis();
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
    
    M5.Lcd.printf("Accel: %.2f\n", accelMagnitude);
    M5.Lcd.printf("HR: %d BPM\n", heartRate);
    M5.Lcd.printf("Batt: %d%%\n", batteryLevel);
    
    // Connection status
    M5.Lcd.setCursor(0, 100);
    M5.Lcd.printf("WiFi: %s\n", wifiConnected ? "OK" : "NO");
    M5.Lcd.printf("MQTT: %s\n", mqttConnected ? "OK" : "NO");
  }

  // User state
  M5.Lcd.setCursor(0, 120);
  switch (userState) {
    case STANDING:
      M5.Lcd.println("State: STANDING");
      break;
    case FALLEN:
      M5.Lcd.println("State: FALLEN");
      break;
  }
}

void updateSensors() {
  heartRate = random(65, 85);  // Simulated heart rate
  batteryLevel = getBatteryPercentage(); // Real battery reading
}

int getBatteryPercentage() {
  float batVoltage = M5.Axp.GetVbatData() * 1.1 / 1000;
  int percentage = (batVoltage - 3.0) / (4.2 - 3.0) * 100;
  return constrain(percentage, 0, 100); // Clamp to 0-100%
}

void publishData() {
  if (mqttConnected) {
    // Publish heart rate
    char heartRateMsg[10];
    sprintf(heartRateMsg, "%d", heartRate);
    client.publish(topic_heartrate, heartRateMsg);
    
    // Publish battery level
    char batteryMsg[10];
    sprintf(batteryMsg, "%d", batteryLevel);
    client.publish(topic_battery, batteryMsg);
  }
}

void reconnect() {
  if (client.connect("M5StickC_Fall_Detector", mqtt_user, mqtt_password)) {
    mqttConnected = true;
    client.subscribe(topic_fall); // Optional: Subscribe to commands
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Handle incoming MQTT messages (if needed)
}

void fall_detection() {
  switch (userState) {
    case STANDING:
      if (accelMagnitude > impactThreshold) {
        userState = FALLEN;
        M5.Lcd.fillScreen(RED);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.println("FALL DETECTED!");
        
        M5.Beep.beep();  // Alert sound
        delay(1000);
        M5.Beep.mute();

        if (mqttConnected) {
          client.publish(topic_fall, "Fall Detected!");
        }
        delay(debounceTime);
      }
      break;

    case FALLEN:
      // Manual recovery button (press A to stand up)
      if (M5.BtnA.wasPressed()) {
        userState = STANDING;
        M5.Lcd.fillScreen(BLUE);
        M5.Lcd.println("RECOVERED!");
        
        if (mqttConnected) {
          client.publish(topic_recovery, "User has stood up!");
        }
      }
      break;
  }
}