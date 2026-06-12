/* ================== Motor and Pump controls ==================
    Version: 1.0
    Details: This includes both remote control and web UI control.
      - Motor Control
      - Pump Control
    Board
      Name: esp32 (ESP32 Dev Module)
      Version: 2.0.6

*/

#include <WiFi.h>
#include <PubSubClient.h> // Version: 2.8,  Author: Nick O'Leary
#include <IBusBM.h>       // Version: 1.14, Author: Bart Mellink

// ESP32 pin directory
#define L_DAC       25
#define R_DAC       26
#define REV_LEFT    32
#define REV_RIGHT   33
#define SP_HIGH     23
#define SP_LOW      22       
#define BRAKE       21

#define IBUS_RX_PIN 16
#define PUMP_PIN    17    

// Constant variables
const float Vmin = 0.84;            // controller idle
const float Vmax = 3.30;            // limited by ESP32 DAC
const float VdacMax = 3.30;

const int ON  = 0;

bool is_armed = false;
int  mode     = 0;

// WiFi Credentials --------------------------------+
  const char* ssid        = "Linksys08736";
  const char* password    = "kbygr7ycna";
  WiFiClient espClient;

// MQTT Server Details -----------------------------+
  const char* mqtt_server = "192.168.1.104";
  const int   mqtt_port   = 1883;
  PubSubClient client(espClient);

  // MQTT TOPICS:
  const char* MQTT_MOVE_CMD = "robot/control";
  const char* MQTT_PUMP_CMD = "robot/pump";

// Function Prototypes -----------------------------+
  void setThrottle(int dacPin, float duty01);
  void set3Speed(int gear);

  // For the Raspberry Pi
  void setupWiFi();
  void reconnect();
  void callback(char* topic, byte* payload, unsigned int length);

// READ IBUS CHANNEL -------------------------------+
IBusBM ibus;
int readChannel(byte channelInput, int minLimit, int maxLimit, int defaultValue) {
  uint16_t ch = ibus.readChannel(channelInput);
  if (ch < 100) return defaultValue;
  return map(ch, 1000, 2000, minLimit, maxLimit);
}

// SETUP -------------------------------------------+
void setup() 
{
  // Initialize a seraial monitor for debugging
    Serial.begin(115200);

    delay(5000); // Allow power stabilization

  // Set the pins
    pinMode(BRAKE, OUTPUT);
    pinMode(REV_LEFT, OUTPUT);
    pinMode(REV_RIGHT, OUTPUT);
    pinMode(PUMP_PIN, OUTPUT);
    pinMode(SP_HIGH, INPUT);
    pinMode(SP_LOW, INPUT);

  // IBus (RX-only)
    Serial2.begin(115200, SERIAL_8N1, IBUS_RX_PIN, -1);
    delay(100);  // small pause helps stabilize
    ibus.begin(Serial2);
    Serial.println("iBus receiver initialized.");

  // Start: all pin at low
    digitalWrite(REV_LEFT, LOW);
    digitalWrite(REV_RIGHT, LOW);
    digitalWrite(BRAKE, LOW);
    digitalWrite(PUMP_PIN, LOW);

    //Both motors start at NO throttle
    setThrottle(L_DAC, 0);
    setThrottle(R_DAC, 0);

    // Speed: 0(low), 50(medium), 100(high) 
    set3Speed(0);
  Serial.println("All commands off");
  delay(50);

  // Initialize WiFi + MQTT
    setupWiFi();
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);

  delay(1000);
}

// LOOP --------------------------------------------+ 
void loop() 
{
  // Read the reciever channels
    //    This are switches: ON = 0, OFF = 100
      int armed = readChannel(4, 0, 100, 0);  // Switch:  SWA
      /*  This switch is for assigning priority
          between remote control and Web UI control */
      mode      = readChannel(5, 0, 100, 0);   // Switch:  SWB
      int speed = readChannel(6,0, 100, 0);     // Switch:  SWC
      int pump  = readChannel(7, 0, 100, 0);  // Switch:  SWD

    //    This is the right joystick
      int forward = readChannel(1, -100, 100, 0); // Y-axis
      int turn    = readChannel(0, -100, 100, 0); // X-axis

  if (armed == ON){
    if (!is_armed){
      digitalWrite(BRAKE, LOW);
      Serial.println("robot is armed");
      is_armed = true;
    }

    if (mode == ON){    // Web Mode
      if (!client.connected()) { 
          reconnect(); 
      } else {
          client.loop();
          delay(500);
      }
    } else{             // Remote Mode
      // Pump control
      if (pump == 0){
        digitalWrite(PUMP_PIN, LOW);
      } else {
        digitalWrite(PUMP_PIN, HIGH);
      }

      // Speed: 0(low), 50(medium), 100(high) 
      set3Speed(speed);
      delay(50);

      // Add Deadzones to the joystick
      if (forward < 5 && forward > -5)
        forward = 0;
      if (turn < 10 && turn > -10)
        turn = 0;

      // Apply actual left and right turns
      if (turn > 0){                //Right Turn
        digitalWrite(REV_LEFT, HIGH);
        digitalWrite(REV_RIGHT, HIGH);
      } else if (turn < 0) {        //Left Turn
        digitalWrite(REV_LEFT, LOW);
        digitalWrite(REV_RIGHT, LOW);
      } else if (forward > 0){      //FORWARD
        digitalWrite(REV_LEFT, LOW);
        digitalWrite(REV_RIGHT, HIGH);
      } else {                      //REVERSE
        digitalWrite(REV_LEFT, HIGH);
        digitalWrite(REV_RIGHT, LOW);
      }
      delay(50);
      
      // Giving that we can get negative value from the joystick
      float steps = abs(forward);

      setThrottle(L_DAC, steps/100);
      setThrottle(R_DAC, steps/100);
    }

  } else{
      if (is_armed){
        digitalWrite(BRAKE, HIGH);
        delay(50);
        digitalWrite(PUMP_PIN, LOW);
        Serial.println("robot is disarmed");

        //Both motors start at NO throttle
        setThrottle(L_DAC, 0);
        setThrottle(R_DAC, 0);

        // Speed: 0(low), 50(medium), 100(high) 
        set3Speed(0);
        is_armed = false;
    }
  }

  delay(100);
}

// ---------------- WIFI & MQTT FUNCTIONS ----------------
void setupWiFi() {
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  Serial.print("Attempting MQTT connection...");
  if (client.connect("ESP32Client1")) { 
    Serial.println("connected");
    client.subscribe(MQTT_MOVE_CMD);
    client.subscribe(MQTT_PUMP_CMD);
  } else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again in 5 seconds");
    delay(5000);
  }
}

void callback(char* topic, byte* payload, unsigned int length){
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.println("]: ");

  //MQTT_MOVE_CMD = "robot/control";
  if (strcmp(topic, MQTT_MOVE_CMD) == 0){
    char move_message[8] = {0};
    memcpy(move_message, payload, min(length, sizeof(move_message)-1));

    // speed
    if (strcmp(move_message, "0") == 0) {
        set3Speed(0);
        Serial.println("Speed: 1");
    }
    else if (strcmp(move_message, "1") == 0) {
        set3Speed(50);
        Serial.println("Speed: 2");
    }
    else if (strcmp(move_message, "2") == 0) {
        set3Speed(100);
        Serial.println("Speed: 3");
    }
    
    // DIRECTION
    if(strcmp(move_message, "D") == 0){
        digitalWrite(REV_LEFT, LOW);
        digitalWrite(REV_RIGHT, HIGH);
        
        setThrottle(L_DAC, 1);
        setThrottle(R_DAC, 1);
    }
    else if(strcmp(move_message, "B") == 0){
        digitalWrite(REV_LEFT, HIGH);
        digitalWrite(REV_RIGHT, LOW);
        
        setThrottle(L_DAC, 1);
        setThrottle(R_DAC, 1);
    }
    else if(strcmp(move_message, "L") == 0){
        digitalWrite(REV_LEFT, LOW);
        digitalWrite(REV_RIGHT, LOW);
        
        setThrottle(L_DAC, 1);
        setThrottle(R_DAC, 1);
    }
    else if(strcmp(move_message, "R") == 0){
        digitalWrite(REV_LEFT, HIGH);
        digitalWrite(REV_RIGHT, HIGH);
        
        setThrottle(L_DAC, 1);
        setThrottle(R_DAC, 1);
    }
    else if(strcmp(move_message, "P") == 0){
        digitalWrite(BRAKE, LOW);
        
        setThrottle(L_DAC, 0);
        setThrottle(R_DAC, 0);
    }
    delay(50);
  }

  // MQTT_PUMP_CMD = "robot/pump";
  if (strcmp(topic, MQTT_PUMP_CMD) == 0){
    char pump_message[4] = {0};
    memcpy(pump_message, payload, min(length, sizeof(pump_message)-1));
    int PumpCmd = atoi(pump_message);

    if (PumpCmd == 1){ 
      digitalWrite(PUMP_PIN, HIGH);
      Serial.println("PUMP ON");
    } else { 
      digitalWrite(PUMP_PIN, LOW);
      Serial.println("PUMP OFF");
    }
  }
  
}

// ---------------- HELPER FUNCTIONS ----------------
void setThrottle(int dacPin, float duty01) {
  if (duty01 < 0) duty01 = 0;
  if (duty01 > 1) duty01 = 1;
  float v = Vmin + duty01 * (Vmax - Vmin);
  uint8_t code = (uint8_t)((v / VdacMax) * 255.0f);
  dacWrite(dacPin, code);
}

void set3Speed(int gear_value) {
  // Speed: 0(low), 50(medium), 100(high) 

  // default:
  digitalWrite(SP_LOW, HIGH);
  digitalWrite(SP_HIGH, HIGH);
  // next speeds:
  if (gear_value == 0)     
    digitalWrite(SP_LOW, LOW);

  if (gear_value == 50)
    digitalWrite(SP_HIGH, LOW);

}
