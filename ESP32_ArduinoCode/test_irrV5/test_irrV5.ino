// ======================================
// ======== Motor & Pump Controls ========
// ======================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <IBusBM.h>

// WiFi Credentials --------------------------------+
const char* ssid        = "Linksys08736";
const char* password    = "kbygr7ycna";
WiFiClient espClient;

// MQTT Server Details -----------------------------+
const char* mqtt_server = "192.168.1.104";
const int   mqtt_port   = 1883;
PubSubClient client(espClient);

  //MQTT TOPICS:
    const char* MQTT_MOVE_CMD = "robot/control";
    const char* MQTT_PUMP_CMD = "robot/pump";

// ==================================================

// ==================================================
// Function Prototypes -----------------------------+
void setupWiFi();
void reconnect();
void setThrottle(int dacPin, float duty01);
void pumpControls(bool on);
void set3Speed(int gear);
void setReverse(bool side, bool rev);
void setBrake(bool on);
void move_control(int forward, int steer, bool stop);
void callback(char* topic, byte* payload, unsigned int length);
void ibusLoop();


IBusBM ibus;
int mode = 0; //Ibus Controls (Global Varibales)
bool armed = false;

// ---------------- PIN DEFINITIONS ----------------

// ✓ Choose new pin for pump (22 is used for SP_LOW)
//#define PUMP_PIN    27     // <-- choose any free pin

#define IBUS_RX_PIN 16

// DAC
#define L_DAC 25
#define R_DAC 26

#define PUMP_PIN    27     // <-- choose any free pin

// Motor control pins
#define REV_LEFT   32
#define REV_RIGHT  33
#define SP_HIGH    23
#define SP_LOW     22       // keep SP_LOW here
#define BRAKE      21

const float Vmin    = 0.84;
const float Vmax    = 3.30;
const float VdacMax = 3.30;

const bool LEFTM = true;
const bool RIGHTM = false;

float L_throttle;
float R_throttle;

bool L_rev;
bool R_rev;

bool pumpState = false;        // actual pump ON/OFF
bool lastPumpSwitch = false;  // previous switch position

// ---------------- READ IBUS CHANNEL ----------------
int readChannel(byte channelInput, int minLimit, int maxLimit, int defaultValue) {
  uint16_t ch = ibus.readChannel(channelInput);
  if (ch < 100) return defaultValue;
  return map(ch, 1000, 2000, minLimit, maxLimit);
}

// ---------------- SETUP ----------------
void setup() {

  /* Debugging: Due to both roboclaw and serial monitor using Serial 0,
          - Comment our instances of roboclaw
          - Uncomment that that contain serial.print or .println
  */
  Serial.begin(115200);
  // ---------------------------------------------------------------

  delay(3000); // Allow power stabilization

  // INITIALIZE: 
  // Define Pins
  pinMode(REV_LEFT, OUTPUT);
  pinMode(REV_RIGHT, OUTPUT);
  pinMode(SP_HIGH, OUTPUT);
  pinMode(SP_LOW, OUTPUT);
  pinMode(BRAKE, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);


  // Initialize WiFi + MQTT
  setupWiFi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  delay(1000);

  //  IBus (RX-only), pin is setup
    Serial2.begin(115200, SERIAL_8N1, IBUS_RX_PIN, -1);
    delay(100);  // small pause helps stabilize
    ibus.begin(Serial2);
    Serial.println("iBus receiver initialized.");

  //  All commands must be low or off
    digitalWrite(REV_LEFT, LOW);
    digitalWrite(REV_RIGHT, LOW);
    digitalWrite(SP_HIGH, LOW);
    digitalWrite(SP_LOW, LOW);
    digitalWrite(BRAKE, LOW);
    set3Speed(1);
    digitalWrite(PUMP_PIN, LOW);
    Serial.println("All commands off");

  delay(1000);
}


// ---------------- MAIN LOOP ----------------
void loop() 
{
  int arm = readChannel(4, 0, 100, 0);  // Channel 5 on IBus SWA
  mode = readChannel(5, 0, 100, 0);     // Channel 6 on IBus SWB

  if (arm != 0) {
    if(!armed) {
        Serial.println("Armed!");
        armed = true;
        setBrake(false); // release brake when armed
    }

    if (mode != 0) {
      // Web Mode
      //Serial.println("Web Mode");
      if (!client.connected()) { 
          reconnect(); 
      } else {
          client.loop();
          delay(500);
      }
    } else {
      // Controller Mode
      ibusLoop();
    }

  } else {
    if(armed) {
      // Only stop motors **once** when disarmed
      Serial.println("Disarmed! Stopping motors.");
      setThrottle(L_DAC, 0);
      setThrottle(R_DAC, 0);
      digitalWrite(REV_LEFT, LOW);
      digitalWrite(REV_RIGHT, LOW);
      digitalWrite(SP_HIGH, LOW);
      digitalWrite(SP_LOW, LOW);
      digitalWrite(PUMP_PIN, LOW);
      setBrake(true); // brake on
      armed = false;
    }
    delay(50); // small delay to reduce CPU hammering
  }
}

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

//Reconnects to the MQTT broker if disconnected.
void reconnect() {
  Serial.print("Attempting MQTT connection...");

  // Attempt to connect with username and password
  if (client.connect("ESP32Client1")) { // , "robot", "robot1"
    Serial.println("connected");
    client.subscribe(MQTT_MOVE_CMD);
    client.subscribe(MQTT_PUMP_CMD);
    Serial.print("Subscribed to topic: ");
    Serial.println(MQTT_MOVE_CMD);
    Serial.println(MQTT_PUMP_CMD);
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

  // Recieve MOTOR'S COMMAND
  if (strcmp(topic, MQTT_MOVE_CMD) == 0){
    // Convert payload to String
    char move_message[8] = {0};
    memcpy(move_message, payload, min(length, sizeof(move_message)-1));


    // --- Speed mode ---
    if (strcmp(move_message, "0") == 0) {
        set3Speed(1);
        Serial.println("Speed: 1");
    }
    else if (strcmp(move_message, "1") == 0) {
        set3Speed(2);
        Serial.println("Speed: 2");
    }
    else if (strcmp(move_message, "2") == 0) {
        set3Speed(3);
        Serial.println("Speed: 3");
    }

    // Movement ------------------------
    int rasp_forward = 0;
    int rasp_steer = 0;
    //Forward
    if(strcmp(move_message, "D") == 0){
      rasp_forward = 50;
      rasp_steer  = 0;

      move_control(rasp_forward, rasp_steer, false);
    }
    //Backward
    else if(strcmp(move_message, "B") == 0){
      rasp_forward = -50;
      rasp_steer  = 0;
      move_control(rasp_forward, rasp_steer, false);
    }
    //Left
    else if(strcmp(move_message, "L") == 0){
      rasp_forward = 0;
      rasp_steer  = -50;
      move_control(rasp_forward, rasp_steer, false);
    }
    //Right
    else if(strcmp(move_message, "R") == 0){
      rasp_forward = 0;
      rasp_steer  = 50;
      move_control(rasp_forward, rasp_steer, false);
    }
    //STOP
    else if(strcmp(move_message, "P") == 0){
      rasp_forward = 0;
      rasp_steer  = 0;
      move_control(rasp_forward, rasp_steer, true);
    }
  }

  // Recieve PUMP'S COMMAND
  if (strcmp(topic, MQTT_PUMP_CMD) == 0){
    char pump_message[4] = {0};
    memcpy(pump_message, payload, min(length, sizeof(pump_message)-1));

    int PumpCmd = atoi(pump_message);

    if (PumpCmd == 1){ 
      digitalWrite(PUMP_PIN, HIGH);
      Serial.println("PUMP ON");
    }
    else{ 
      digitalWrite(PUMP_PIN, LOW);
      Serial.println("PUMP OFF");
    }
  }
}

void ibusLoop()
{
  int pump_channel = readChannel(7, 0, 100, 0);  // IBus SWD
  int forward = readChannel(1, -50, 50, 0);
  int steer   = readChannel(0, -50, 50, 0);
  int speed_channel = readChannel(6, 0, 100, 0);

  // Below write any Ibus interaction with components
  // FOR: pump
  bool pumpSwitch = (pump_channel == 100);

  if (pumpSwitch != lastPumpSwitch) {
      pumpControls(pumpSwitch);
      Serial.println(pumpSwitch ? "Pump ON (IBUS)" : "Pump OFF (IBUS)");
  }

  lastPumpSwitch = pumpSwitch;

  //FOR: drive
  // SET SPEED ---------------------------------------+
  if (speed_channel == 0) set3Speed(1);
  else if (speed_channel == 100) set3Speed(2);
  else set3Speed(3);

  // SET STEERING ------------------------------------+
  L_throttle = forward - steer;
  R_throttle = forward + steer;

  L_throttle = (L_throttle > 5 || L_throttle < -5) ? L_throttle : 0;
  R_throttle = (R_throttle > 5 || R_throttle < -5) ? R_throttle : 0;

  if(L_throttle < 0) {setReverse(LEFTM, true); L_throttle = -L_throttle; L_rev=true;}
  else {setReverse(LEFTM, false); L_rev=false;}
  if(R_throttle < 0) {setReverse(RIGHTM, true); R_throttle = -R_throttle; R_rev=true;}
  else {setReverse(RIGHTM, false); R_rev=false;}
  
  // SET THROTTLE ------------------------------------+
  setThrottle(L_DAC, (L_throttle/100));
  setThrottle(R_DAC, (R_throttle/100));


  Serial.println(
    String("Speed: ") + speed_channel +
    " | Forward: " + forward +
    " | Steer: " + steer +
    " | Left Th: " + L_throttle +
    " | Right Th: " + R_throttle +
    " | Left Rev: " + L_rev +
    " | Right Rev: " + R_rev 
  );

  delay(30);
}

  // ---------------- MOTOR CONTROL FUNCTIONS ----------------
void setThrottle(int dacPin, float duty01) {
  if (duty01 < 0) duty01 = 0;
  if (duty01 > 1) duty01 = 1;
  float v = Vmin + duty01 * (Vmax - Vmin);
  uint8_t code = (uint8_t)((v / VdacMax) * 255.0f);
  dacWrite(dacPin, code);
}

void set3Speed(int gear) {
  bool H=false, L=false;

  if (gear==1) L=true;       // low
  else if (gear==3) H=true;  // mid
  else{
    H = false; L = false;
  }

  digitalWrite(SP_HIGH, H ? HIGH : LOW);
  digitalWrite(SP_LOW,  L ? HIGH : LOW);
}

void setReverse(bool side, bool rev){
  // Left = True Right = False
  
  digitalWrite(side ? REV_LEFT : REV_RIGHT,  rev ? HIGH : LOW);
    
}

void setBrake(bool on){
  digitalWrite(BRAKE, on ? HIGH : LOW);
}

void move_control(int forward, int steer, bool stop)
{
  Serial.print("forward: ");
  Serial.print(forward);
  Serial.print(" | steer:   ");
  Serial.print(steer);
  Serial.print(" | stop:    ");
  Serial.println(stop);

  float L_throttle = forward + steer;
  float R_throttle = forward - steer;

  if(L_throttle < 0) {setReverse(LEFTM, true); L_throttle = -L_throttle;}
  else {setReverse(LEFTM, false);}
  if(R_throttle < 0) {setReverse(RIGHTM, true); R_throttle = -R_throttle;}
  else {setReverse(RIGHTM, false);}

  if(!stop){
    setThrottle(L_DAC, (1));
    setThrottle(R_DAC, (1));
  }
  else{
    setThrottle(L_DAC, (0));
    setThrottle(R_DAC, (0));
  }
}

void pumpControls(bool on) {
    digitalWrite(PUMP_PIN, on ? HIGH : LOW);
}
