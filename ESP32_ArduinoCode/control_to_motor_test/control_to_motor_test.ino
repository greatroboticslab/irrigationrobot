// Test for controlling both motor and pump control 
// through an external remote control

// Library for the reciever
// Version: 1.1.4
#include <IBusBM.h>

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
const int OFF = 100;

bool is_armed = false;

// ---------------- READ IBUS CHANNEL ----------------
IBusBM ibus;
int readChannel(byte channelInput, int minLimit, int maxLimit, int defaultValue) {
  uint16_t ch = ibus.readChannel(channelInput);
  if (ch < 100) return defaultValue;
  return map(ch, 1000, 2000, minLimit, maxLimit);
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

  if (gear == 50)
    digitalWrite(SP_HIGH, LOW);

}

// ---------------- SETUP ----------------
void setup() {
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

  delay(1000);
}

// ---------------- LOOPS ----------------
void loop() {
  // Read the reciever channels
    //    This are switches: ON = 0, OFF = 100
      int armed = readChannel(4, 0, 100, 0);  // Switch:  SWA
      /*  This switch is for assigning priority
          between remote control and Web UI control */
      // int mode = readChannel(5, 0, 100, 0);   // Switch:  SWB
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
