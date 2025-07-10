#include <Arduino.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif

#include <Firebase_ESP_Client.h>

// Firebase helper files
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// WiFi and Firebase credentials
#define WIFI_SSID "SPGalaxy"
#define WIFI_PASSWORD "abcd1234"
#define API_KEY "AIzaSyAuolUSQiOFY4n5e176v8jvPUAUp_tSuaI"
#define DATABASE_URL "https://sentinelgaurd-2b108-default-rtdb.asia-southeast1.firebasedatabase.app/"

// Sensor pins
#define DHTPIN 4
#define DHTTYPE DHT22
#define MQ135_PIN 34
#define MQ6_PIN 35
#define MQ7_PIN 32
#define IR_SENSOR_PIN 23
#define BUZZER_PIN 27
#define SOS_BUTTON_PIN 14

// Switch pins
#define SWITCH_DHT 5
#define SWITCH_MQ135 18
#define SWITCH_MQ6 19
#define SWITCH_MQ7 15
#define SWITCH_IR 13

LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHTPIN, DHTTYPE);

// Firebase setup
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

// LCD update tracking
String currentDisplay = "";
unsigned long lastDisplayUpdate = 0;
const unsigned long displayHoldTime = 3000;  // milliseconds

void setupFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase connection successful.");
    signupOK = true;
  } else {
    Serial.printf("Firebase signup failed: %s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

bool sendDataToFirebase(String path, float value) {
  if (Firebase.ready() && signupOK) {
    if (Firebase.RTDB.setFloat(&fbdo, path.c_str(), value)) {
      Serial.println("Sent: " + path + " = " + String(value));
      return true;
    } else {
      Serial.println("Failed to send data. Reason: " + fbdo.errorReason());
    }
  }
  return false;
}

void alarmBuzzer(bool state) {
  if (state) tone(BUZZER_PIN, 1000);
  else noTone(BUZZER_PIN);
}

void updateLCD(String line1, String line2) {
  if ((millis() - lastDisplayUpdate > displayHoldTime) || (line1 + line2 != currentDisplay)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(line1);
    lcd.setCursor(0, 1);
    lcd.print(line2);
    currentDisplay = line1 + line2;
    lastDisplayUpdate = millis();
    delay(1000);  // Adding 1-second delay after LCD update
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");

  setupFirebase();

  // Initialize pins
  pinMode(SWITCH_DHT, INPUT_PULLUP);
  pinMode(SWITCH_MQ135, INPUT_PULLUP);
  pinMode(SWITCH_MQ6, INPUT_PULLUP);
  pinMode(SWITCH_MQ7, INPUT_PULLUP);
  pinMode(SWITCH_IR, INPUT_PULLUP);
  pinMode(SOS_BUTTON_PIN, INPUT_PULLUP);
  pinMode(IR_SENSOR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  dht.begin();

  Wire.begin(21, 22);
  lcd.begin(16, 2);
  lcd.backlight();
  lcd.clear();
  updateLCD("System Booting", "Please wait...");
  delay(2000);

  Serial.println("System Ready.");
}

void loop() {
  int switchDHTState = digitalRead(SWITCH_DHT);
  int switchMQ135State = digitalRead(SWITCH_MQ135);
  int switchMQ6State = digitalRead(SWITCH_MQ6);
  int switchMQ7State = digitalRead(SWITCH_MQ7);
  int switchIRState = digitalRead(SWITCH_IR);
  int sosButtonState = digitalRead(SOS_BUTTON_PIN);

  bool alarmTrigger = false;

  // SOS Button
  if (sosButtonState == LOW) {
    updateLCD("!!! SOS ALERT !!!", "");
    alarmBuzzer(true);
    sendDataToFirebase("/sensor/sosAlert", 1);
    delay(3000);  // Hold alert on screen
  } else {
    sendDataToFirebase("/sensor/sosAlert", 0);
  }

  // DHT Sensor
  if (switchDHTState == LOW) {
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    if (!isnan(temp) && !isnan(hum)) {
      sendDataToFirebase("/sensor/temperature", temp);
      sendDataToFirebase("/sensor/humidity", hum);
      updateLCD("Temp: " + String(temp, 1) + "C", "Hum: " + String(hum, 1) + "%");
    }
  }

  // MQ135 Sensor
  if (switchMQ135State == LOW) {
    int air = analogRead(MQ135_PIN);
    sendDataToFirebase("/sensor/airQuality", air);
    updateLCD("Air Quality:", String(air));
    if (air > 5000) alarmTrigger = true;
  }

  // MQ6 Sensor
  
  // Simulate dummy gas level around 5 ppm
  float gas = random(450, 550) / 100.0;  // Generates float between 4.5 and 5.49
  sendDataToFirebase("/sensor/gasLevel", gas);
  updateLCD("LPG Level:", String(gas, 2) + " ppm");

  // MQ7 Sensor
  if (switchMQ7State == LOW) {
    int co = analogRead(MQ7_PIN);
    sendDataToFirebase("/sensor/coLevel", co);
    updateLCD("CO Level:", String(co));
    if (co > 4000) alarmTrigger = true;
  }

  // IR Sensor
  if (switchIRState == LOW) {
    int ir = digitalRead(IR_SENSOR_PIN);
    if (ir == LOW) {
      sendDataToFirebase("/sensor/obstacleDetected", 1);
      updateLCD("Obstacle", "Detected!");
      alarmTrigger = true;
    } else {
      sendDataToFirebase("/sensor/obstacleDetected", 0);
      updateLCD("No Obstacle", "");
    }
  }

  alarmBuzzer(alarmTrigger);

  delay(100);  // Small delay to avoid looping too fast
}
