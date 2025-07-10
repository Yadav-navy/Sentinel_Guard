#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <SoftwareSerial.h>
#include <TinyGPS++.h>
#include <Firebase_ESP_Client.h>

// ------------------- WiFi & Ubidots -------------------
#define WIFISSID "SPGalaxy"
#define PASSWORD "abcd1234"

#define TOKEN "BBUS-kxI5PPUCZ5XOQbeT26yslhEaQp1q0M"  
#define MQTT_CLIENT_NAME "ESP8266_ECG_Client"

#define DEVICE_LABEL "Ecg_Monitor"
#define VARIABLE_LABEL "ECG_data"

char mqttBroker[] = "industrial.api.ubidots.com";
char payload[1000];
char topic[150];
char str_sensor[10];
char str_millis[20];

WiFiClient ubidots;
PubSubClient client(ubidots);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

double epochseconds = 0;
double epochmilliseconds = 0;
double current_millis = 0;
double current_millis_at_sensordata = 0;
double timestampp = 0;
int j = 0;

// ------------------- ECG Sensor -------------------
#define SENSORPIN A0

// ------------------- GPS Setup -------------------
static const int RXPin = 13; // D7
static const int TXPin = 15; // D8
static const uint32_t GPSBaud = 9600;

TinyGPSPlus gps;
SoftwareSerial ss(RXPin, TXPin);

// ------------------- Firebase -------------------
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define API_KEY "AIzaSyAuolUSQiOFY4n5e176v8jvPUAUp_tSuaI"
#define DATABASE_URL "https://sentinelgaurd-2b108-default-rtdb.asia-southeast1.firebasedatabase.app/"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

unsigned long lastGpsSent = 0;
const unsigned long gpsInterval = 10000; // 10 seconds

// ------------------- MQTT Callback -------------------
void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println((char *)payload);
}

// ------------------- MQTT Reconnect -------------------
void reconnect() {
  while (!client.connected()) {
    Serial.println("Attempting MQTT connection...");
    if (client.connect(MQTT_CLIENT_NAME, TOKEN, "")) {
      Serial.println("Connected to MQTT");
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" trying again in 2 seconds...");
      delay(2000);
    }
  }
}

// ------------------- Setup -------------------
void setup() {
  Serial.begin(115200);
  pinMode(SENSORPIN, INPUT);
  ss.begin(GPSBaud);
  WiFi.begin(WIFISSID, PASSWORD);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // Ubidots setup
  timeClient.begin();
  timeClient.update();
  epochseconds = timeClient.getEpochTime();
  epochmilliseconds = epochseconds * 1000;
  current_millis = millis();

  client.setServer(mqttBroker, 1883);
  client.setCallback(callback);

  // Firebase setup
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase connected.");
    signupOK = true;
  } else {
    Serial.printf("Firebase SignUp Failed: %s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

// ------------------- Loop -------------------
void loop() {
  // -------- ECG: Send to Ubidots --------
  if (!client.connected()) {
    reconnect();
    j = 0;
  }

  j++;
  sprintf(topic, "/v1.6/devices/%s", DEVICE_LABEL);
  String payloadStr = "{\"";
  payloadStr += VARIABLE_LABEL;
  payloadStr += "\": [";

  for (int i = 1; i <= 3; i++) {
    float sensor = analogRead(SENSORPIN);
    dtostrf(sensor, 4, 2, str_sensor);
    current_millis_at_sensordata = millis();
    timestampp = epochmilliseconds + (current_millis_at_sensordata - current_millis);
    dtostrf(timestampp, 10, 0, str_millis);

    payloadStr += "{\"value\":";
    payloadStr += str_sensor;
    payloadStr += ", \"timestamp\": ";
    payloadStr += str_millis;
    payloadStr += "}";

    if (i < 3) payloadStr += ",";
    delay(150);
  }

  payloadStr += "]}";
  payloadStr.toCharArray(payload, 1000);

  Serial.println("Sending ECG payload:");
  Serial.println(payload);

  client.publish(topic, payload);
  client.loop();

  // -------- GPS: Send to Firebase every 10s --------
  while (ss.available() > 0) {
    gps.encode(ss.read());
    if (gps.location.isUpdated()) {
      float latitude = gps.location.lat();
      float longitude = gps.location.lng();

      Serial.print("Latitude: ");
      Serial.println(latitude, 6);
      Serial.print("Longitude: ");
      Serial.println(longitude, 6);

      if (Firebase.ready() && signupOK && millis() - lastGpsSent > gpsInterval) {
        bool latSent = Firebase.RTDB.setFloat(&fbdo, "/gps/latitude", latitude);
        bool lonSent = Firebase.RTDB.setFloat(&fbdo, "/gps/longitude", longitude);

        if (latSent && lonSent) {
          Serial.println("GPS data sent to Firebase successfully!");
        } else {
          Serial.printf("Error sending GPS data: %s\n", fbdo.errorReason().c_str());
        }
        lastGpsSent = millis();
      }
    }
  }
}
