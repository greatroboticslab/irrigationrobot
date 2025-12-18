#include <WiFi.h>
#include <PubSubClient.h>

// WiFi credentials
const char* ssid = "downRobotRoom";
const char* password = "robotsRcool";

// MQTT broker info
const char* mqtt_server = "192.168.1.145";
const int mqtt_port = 1883;

// Moisture sensor pin
#define SENSOR_PIN 36

// Moisture calibration values
int dryValue = 2200;
int wetValue = 1200;

WiFiClient espClient;
PubSubClient client(espClient);

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT @ ");
    Serial.println(mqtt_server);
    if (client.connect("MoistureSenderESP")) {
      Serial.println("MQTT connected.");
    } else {
      Serial.print("Failed, rc= ");
      Serial.print(client.state());
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Sender ESP Starting...");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  client.setServer(mqtt_server, mqtt_port);
}

void loop() {
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();

  // Read and map the sensor value
  int moistureRaw = analogRead(SENSOR_PIN);
  int moisturePercent = map(moistureRaw, dryValue, wetValue, 1, 100);
  moisturePercent = constrain(moisturePercent, 1, 100);

  // Get device MAC address
  String mac = WiFi.macAddress();

  // Create MQTT payload
  char payload[128];
  snprintf(payload, sizeof(payload), "{\"mac\":\"%s\", \"value\":%d}", mac.c_str(), moisturePercent);

  Serial.println(payload);
  client.publish("moisture/data", payload);

  delay(2000);
}
