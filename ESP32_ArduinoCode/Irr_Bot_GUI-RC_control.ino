#include <WiFi.h>
#include <PubSubClient.h>
#include <RoboClaw.h>
#include <IBusBM.h>

// WiFi credentials
const char* ssid = "Linksys08736";         // Replace with your WiFi SSID
const char* password = "kbygr7ycna";       // Replace with your WiFi password

// MQTT server details
const char* mqtt_server = "192.168.1.104";  // Replace with the IP address of your Raspberry Pi
const int mqtt_port = 1883;                 // Default MQTT port

WiFiClient espClient;
PubSubClient client(espClient);

// Define the MQTT topic to subscribe to
const char* MQTT_MOVE_CMD = "robot/control";
const char* MQTT_RAIL_CMD = "robot/rail";
const char* MQTT_PUMP_CMD = "robot/pump";


// RoboClaw setup
RoboClaw roboclaw(&Serial2, 10000);  // Use Serial2 for communication with RoboClaw

// RoboClaw addresses
#define ROBOCLAW_ADDRESS_LEFT  0x80  // Address for the left motor's RoboClaw  = 128
#define ROBOCLAW_ADDRESS_RIGHT 0x81  // Address for the right motor's RoboClaw = 129
#define ROBOCLAW_ADDRESS_RAIL  0x82  // Address for the rail motor's RoboClaw  = 130

// IBussBM setup
IBusBM ibus;
int mode = 0;

// IBus control for reading RC controls
int readChannel(byte channelInput, int minLimit, int maxLimit, int defaultValue) {
  uint16_t ch = ibus.readChannel(channelInput);
  if (ch < 100) return defaultValue;
  return map(ch, 1000, 2000, minLimit, maxLimit);
}

// Mapping constants
#define STOP_COMMAND 64
#define MAX_COMMAND 126
#define MIN_COMMAND 0

// Pump Controls
#define PUMP_PIN 27

// Function prototypes
void setupWiFi();
void reconnect();
void callback(char* topic, byte* payload, unsigned int length);

// Function to map motor speed
/**
 * @brief Maps a speed value from -127 to +127 to 0-127 for RoboClaw.
 *
 * @param speed Speed value ranging from -127 to +127.
 * @return uint8_t Mapped speed value ranging from 0 to 127.
 */
uint8_t mapMotorSpeed(int speed) {
  int mappedSpeed = speed + 127; // Map from -127~+127 to 0~254
  mappedSpeed = constrain(mappedSpeed, 0, 254);
  return (uint8_t)(mappedSpeed / 2); // Scale down to 0~127
}

// Motor control functions for GUI Control
/**
 * @brief Sets the motor speeds based on forward and turn commands.
 *
 * @param forwardCmd Forward command value (0-126), where 64 is stop.
 * @param turnCmd Turn command value (0-126), where 64 is no turn.
 */
void setMotorSpeeds(int forwardCmd, int turnCmd) {
  if( mode == 0){
    return;
  }
  // Map forward and turn commands from 0-126 to -62 to +62
  int mappedForward = forwardCmd - STOP_COMMAND; // Range: -62 to +62
  int mappedTurn = turnCmd - STOP_COMMAND;       // Range: -62 to +62

  // Calculate left and right motor speeds
  int LSpeed = mappedForward + mappedTurn;
  int RSpeed = mappedForward - mappedTurn;

  // Constrain motor speeds to valid range (-127 to +127)
  LSpeed = constrain(LSpeed, -127, 127);
  RSpeed = constrain(RSpeed, -127, 127);

  // Map speeds to 0-127 for RoboClaw
  uint8_t MappedLSpeed = mapMotorSpeed(LSpeed);
  uint8_t MappedRSpeed = mapMotorSpeed(RSpeed);

  // Debugging output
  /*
  Serial.print("Mapped Forward: ");
  Serial.print(mappedForward);
  Serial.print(" | Mapped Turn: ");
  Serial.print(mappedTurn);
  Serial.print(" | LSpeed: ");
  Serial.print(LSpeed);
  Serial.print(" | RSpeed: ");
  Serial.print(RSpeed);
  Serial.print(" | MappedLSpeed: ");
  Serial.print(MappedLSpeed);
  Serial.print(" | MappedRSpeed: ");
  Serial.println(MappedRSpeed);
  */
  // Set motor speeds on M1 of each RoboClaw controller
  roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_LEFT, MappedLSpeed);   // Left Motor at address 0x80
  roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_RIGHT, MappedRSpeed);  // Right Motor at address 0x81
}

void moveRail(int RailCmd) {
  if( mode == 0){
    return;
  }


  // Constrain motor speeds to valid range (-127 to +127)
  int RailSpeed = constrain(RailCmd, 0, 127);

  // Set motor speeds on M1 of each RoboClaw controller
  roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_RAIL, RailSpeed);   // Rail Motor at address 0x82
  
}

/**
 * @brief Stops both motors.
 */
void stopMotors() {
  
  Serial1.println("Stopping Motors");
  roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_LEFT, STOP_COMMAND);   // Stop Left Motor
  roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_RIGHT, STOP_COMMAND);  // Stop Right Motor

}


void setup() {
  //Serial.begin(115200);  // Initialize Serial for debugging
  Serial1.begin(115200, SERIAL_8N1, -1, 17); // Only TX on GPIO17

  // Initialize WiFi
  setupWiFi();

  // Initialize MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Initialize RoboClaw
  //Serial2.begin(38400);  // Use Serial2 for RoboClaw communication
  roboclaw.begin(115200);
  ibus.begin(Serial); // Need to change this since Roboclaw is using 2
  delay(500);

  // Ensure motors are stopped on startup
  stopMotors();

  // Set up pump control pins and turn the off initially
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);

}

void loop() {
  // Reconnect to MQTT broker if not connected
  if (!client.connected()) {
    reconnect();
  }

  // Handle incoming MQTT messages
  client.loop();

  int ch2 = readChannel(1, 0, 126, 64);
  int ch1 = readChannel(0, 0, 126, 64);
  int ch4 = readChannel(3, 0, 126, 64); // for the rail motor
  int pump_cmd = readChannel(7, 0, 100, 0);
  int arm = readChannel(4, 0, 100, 0); // Channel 5 on IBus SWA
  mode = readChannel(5, 0, 100, 0); // Channel 6 on IBus SWB (Manual = 0 (UP),  Auto = 100 (Down))

  int turn = map(ch1, 0, 126, -63, 63);
  int value = ch2;
  int rail = ch4;

  if (value == 63) {
    value = 64;
  }
  if (rail < 67 && rail > 61) {
    rail = 64;
  }

  int LSpeed = value+turn;
  int RSpeed = value-turn;

  if (LSpeed <= 66 && LSpeed >= 62) LSpeed = 64;
  if (RSpeed <= 66 && RSpeed >= 62) RSpeed = 64;
  if (LSpeed > 126) LSpeed = 126;
  if (RSpeed > 126) RSpeed = 126;
  if (LSpeed < 1) LSpeed = 1;
  if (RSpeed < 1) RSpeed = 1;
  

  if(arm == 0){
    stopMotors();
    digitalWrite(PUMP_PIN, LOW); 
  } else if (mode == 0){
    roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_LEFT, LSpeed);
    roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_RIGHT, RSpeed);
    roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_RAIL, rail);

    if(pump_cmd == 100){  digitalWrite(PUMP_PIN, HIGH); }
    else{  digitalWrite(PUMP_PIN, LOW);  }
  }
  delay(10);

}



/**
 * @brief Connects to the specified WiFi network.
 */
void setupWiFi() {
  Serial1.print("Connecting to ");
  Serial1.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial1.print(".");
  }

  Serial1.println("\nConnected to WiFi");
  Serial1.print("IP Address: ");
  Serial1.println(WiFi.localIP());
}

/**
 * @brief Reconnects to the MQTT broker if disconnected.
 */
void reconnect() {
  while (!client.connected()) {
    Serial1.print("Attempting MQTT connection...");

    // Attempt to connect with username and password
    if (client.connect("ESP32Client1")) { // , "robot", "robot1"
      Serial1.println("connected");
      client.subscribe(MQTT_MOVE_CMD);
      client.subscribe(MQTT_RAIL_CMD);
      client.subscribe(MQTT_PUMP_CMD);
      Serial1.print("Subscribed to topic: ");
      Serial1.println(MQTT_MOVE_CMD);
      Serial1.println(MQTT_RAIL_CMD);
      Serial1.println(MQTT_PUMP_CMD);
    } else {
      Serial1.print("failed, rc=");
      Serial1.print(client.state());
      Serial1.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

/**
 * @brief Callback function that processes incoming MQTT messages.
 *
 * @param topic The topic on which the message was received.
 * @param payload The message payload.
 * @param length The length of the message payload.
 */
void callback(char* topic, byte* payload, unsigned int length) {
  Serial1.print("Message arrived [");
  Serial1.print(topic);
  Serial1.print("]: ");

  if (strcmp(topic, MQTT_MOVE_CMD) == 0){
    // Convert payload to String
    String move_message;
    for (unsigned int i = 0; i < length; i++) {
      move_message += (char)payload[i];
    }

    Serial1.println("Received MOVE message: " + move_message);

    // Parse the message assuming format "forward_command side_command"
    int spaceIndex = move_message.indexOf(' ');
    if (spaceIndex == -1) {
      Serial1.println("Invalid message format. Expected 'forward_command side_command'.");
      stopMotors();
      return;
    }

    String forwardStr = move_message.substring(0, spaceIndex);
    String turnStr = move_message.substring(spaceIndex + 1);

    // Convert strings to integers
    int forwardCmd = forwardStr.toInt();
    int turnCmd = turnStr.toInt();

    // Validate command ranges
    if (forwardCmd < MIN_COMMAND || forwardCmd > MAX_COMMAND ||
        turnCmd < MIN_COMMAND || turnCmd > MAX_COMMAND) {
      Serial1.println("Command values out of range. Expected 0-126.");
      stopMotors();
      return;
    }

    // Debugging output
    Serial1.print("Forward Command: ");
    Serial1.print(forwardCmd);
    Serial1.print(" | Turn Command: ");
    Serial1.println(turnCmd);

    // Determine if the robot should stop
    if (forwardCmd == STOP_COMMAND && turnCmd == STOP_COMMAND) {
      stopMotors();
    } else {
      setMotorSpeeds(forwardCmd, turnCmd);
    }
  }

  if (strcmp(topic, MQTT_RAIL_CMD) == 0){
    String rail_message = "";
    for (int i = 0; i < length; i++) {
      rail_message += (char)payload[i];
    }
    Serial1.println("Received pump message: " + rail_message);

    int RailCmd = rail_message.toInt();

    moveRail(RailCmd); 
  }

  if (strcmp(topic, MQTT_PUMP_CMD) == 0){
    String pump_message = "";
    for (int i = 0; i < length; i++) {
      pump_message += (char)payload[i];
    }
    Serial1.println("Received pump message: " + pump_message);

    int PumpCmd = pump_message.toInt();

    if(PumpCmd == 1){  digitalWrite(PUMP_PIN, HIGH); }
    else{  digitalWrite(PUMP_PIN, LOW);  }
  }

}




























