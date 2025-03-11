#include <M5StickCPlus.h>
#include <WiFi.h>
#include <PubSubClient.h>

// WiFi and MQTT settings
const char* ssid = "Xiaomi Nya"; // wifi hotspot name
const char* password = "itamenekos"; // wifi hotspot password
const char* mqtt_server = "192.168.1.103"; // raspberry pi IP address (run 'hostname -I' on pi terminal or IP Scan)
const int mqtt_port = 1883;
const char* mqtt_user = "zyanpi"; // Your raspberry pi username
const char* mqtt_password = "ilovenekopi"; // Your raspberry pi login password if have   

// Worker and floor information
const char* workerID = "worker1";
const char* floorID = "floor1";

// MQTT topics
char topic_fall[50];
char topic_heartrate[50];
char topic_battery[50];
char topic_recovery[50]; // [ADDED] topic to indicate if the user has recovered from the fall

WiFiClient espClient;
PubSubClient client(espClient);

// Accelerometer settings
float accX, accY, accZ;
float accelMagnitude;
//Not very accurate but might need you guys to help out with tweaking and researching more
//float fallThreshold = 0.5;   // Threshold for detecting free-fall
float impactThreshold = 3.0; // Threshold for detecting impact
//float stableThreshold = 1.0; // Threshold for stable position (upright)

// State machine
enum UserState {STANDING, FALLEN};
UserState userState = STANDING;

bool possibleFall = false;
unsigned long fallDetectionTime = 0;
const int debounceTime = 2000;  // Prevent multiple detections
//const int stabilityCheckTime = 3000; // Time to check stability after a fall

// Simulated heart rate and battery level
int heartRate = 75;
int batteryLevel = 100; // This should be the actual battery read so that the "supervisor" can know who's device need to be changed/charged

unsigned long lastUpdateTime = 0;
unsigned long lastPublishTime = 0;
unsigned long lastReconnectAttempt = 0;

// Status flags
bool wifiConnected = false;
bool mqttConnected = false;

// Moving average for acceleration
const int movingAverageWindow = 10; // [ADDED] Number of samples for moving average
float accelMagnitudeHistory[movingAverageWindow]; // [ADDED]
int accelMagnitudeIndex = 0; // [ADDED]
float accelMagnitudeAverage = 0; // [ADDED]

void setup() {
  M5.begin();
  M5.IMU.Init();
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);

  // Generate dynamic topics
  sprintf(topic_fall, "/%s/%s/falldetect", floorID, workerID);
  sprintf(topic_heartrate, "/%s/%s/heartrate", floorID, workerID);
  sprintf(topic_battery, "/%s/%s/battery", floorID, workerID);
  sprintf(topic_recovery, "/%s/%s/recovery", floorID, workerID); // [ADDED]

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
  M5.Lcd.println(topic_recovery); // [ADDED]
  delay(3000);

  // Initialize moving average array [ADDED]
  for (int i = 0; i < movingAverageWindow; i++) {
    accelMagnitudeHistory[i] = 1.0; // Start with gravity magnitude
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
  M5.update();  // Process button inputs
  
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
  accelMagnitude = sqrt(accX*accX + accY*accY + accZ*accZ);

  // Update moving average [ADDED]
  accelMagnitudeHistory[accelMagnitudeIndex] = accelMagnitude;
  accelMagnitudeIndex = (accelMagnitudeIndex + 1) % movingAverageWindow;
  accelMagnitudeAverage = 0;
  for (int i = 0; i < movingAverageWindow; i++) {
    accelMagnitudeAverage += accelMagnitudeHistory[i];
  }
  accelMagnitudeAverage /= movingAverageWindow;

  // Fall detection algorithm: Look for free-fall followed by impact
  fall_detection();
  
  // Update display every 1 second
  if (millis() - lastUpdateTime > 1000) {
    lastUpdateTime = millis();
    updateDisplay();
  }
  
  // Publish heart rate and battery every 1 second
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
    
    // Display connection status
    M5.Lcd.setCursor(0, 100);
    M5.Lcd.printf("WiFi: %s\n", wifiConnected ? "OK" : "NO");
    M5.Lcd.printf("MQTT: %s\n", mqttConnected ? "OK" : "NO");
  }

  // Display user state
  M5.Lcd.setCursor(0, 120);
  switch (userState) {
    case STANDING:
      M5.Lcd.println("State: STANDING");
      break;
    case FALLEN:
      M5.Lcd.println("State: FALLEN");
      break;
    // case STANDING_UP:
    //   M5.Lcd.println("State: STANDING UP");
    //   break;
  }
}

void updateSensors() {
  // Simulate heart rate (more realistic pattern)
  heartRate = random(65, 85);  // Simulate heart rate between 65-85 bpm
  
  // Read actual battery level instead of simulating
  batteryLevel = getBatteryPercentage();
}

int getBatteryPercentage() {
  float batVoltage = M5.Axp.GetVbatData() * 1.1 / 1000;
  
  // Convert voltage to percentage
  // The voltage range is approximately 3.0V (empty) to 4.2V (full)
  int percentage = (batVoltage - 3.0) / (4.2 - 3.0) * 100;
  
  // Ensure percentage is within 0-100 range
  if (percentage > 100) percentage = 100;
  if (percentage < 0) percentage = 0;
  
  return percentage;
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
    // Resubscribe if needed
    client.subscribe(topic_fall);
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
}

void fall_detection() {
  switch (userState) {
    case STANDING:
      // if (accelMagnitude < fallThreshold) {
      //   // Possible free-fall detected
      //   possibleFall = true;
      //   fallDetectionTime = millis();
      // } else if (possibleFall && (millis() - fallDetectionTime < 1000)) {
      //   // Within 1sec of detecting possible free-fall, look for impact
        
      // } else if (possibleFall && (millis() - fallDetectionTime >= 1000)) {
      //   // Reset if no impact detected within timeframe
      //   possibleFall = false;
      // }

      if (accelMagnitude > impactThreshold) {
          // Impact detected - FALL DETECTED
          userState = FALLEN;
          M5.Lcd.fillScreen(RED);
          M5.Lcd.setCursor(0, 0);
          M5.Lcd.println("FALL DETECTED!");
          M5.Lcd.println("Sending alert...");
          
          // Trigger beep
          M5.Beep.beep();  // Start beeping
          delay(1000);     // Beep for 1 second
          M5.Beep.mute();  // Stop beeping

          // Publish fall detection data if connected
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
      break;

    case FALLEN:
      // Check if the user has returned to a stable position
      // if (accelMagnitude >= stableThreshold) {
        
      //   // M5.Lcd.fillScreen(GREEN);
      //   // M5.Lcd.setCursor(0, 0);
      //   // M5.Lcd.println("STANDING UP!");
      //   //userState = STANDING_UP;

        
      // }

      // Check if 'A' button is pressed to manually indicate standing
      if (M5.BtnA.wasPressed()) {
        userState = STANDING;
        M5.Lcd.fillScreen(BLUE);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.println("STANDING BACK UP!");
        return;
      }
      
      // Publish recovery data if connected
      if (mqttConnected) {
        M5.Lcd.println("Publishing recovery...");
        bool success = client.publish(topic_recovery, "User has stood up!");
        if (success) {
          M5.Lcd.println("Recovery published!");
        } else {
          M5.Lcd.println("Publish failed!");
        }
      }

      break;

    // case STANDING_UP:
    //   // Check if the user is fully standing
    //   if (accelMagnitude >= stableThreshold) {
    //     userState = STANDING;
    //     M5.Lcd.fillScreen(BLUE);
    //     M5.Lcd.setCursor(0, 0);
    //     M5.Lcd.println("STANDING UP!");
    //   }
    //   break;
  }
}