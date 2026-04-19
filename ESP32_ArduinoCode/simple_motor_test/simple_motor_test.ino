//Test proper motor control without any other external controls

// ESP32 pin directory
#define L_DAC 25
#define R_DAC 26
#define REV_LEFT 32
#define REV_RIGHT 33
#define SP_HIGH 23
#define SP_LOW 22       
#define BRAKE 21

const float Vmin = 0.84;            // controller idle
const float Vmax = 3.30;            // limited by ESP32 DAC
const float VdacMax = 3.30;

void setThrottle(int dacPin, float duty01) {
  if (duty01 < 0) duty01 = 0;
  if (duty01 > 1) duty01 = 1;
  float v = Vmin + duty01 * (Vmax - Vmin);
  uint8_t code = (uint8_t)((v / VdacMax) * 255.0f);
  dacWrite(dacPin, code);
}

void setup() {
  Serial.begin(115200);
  // put your setup code here, to run once:
  delay(5000);
  // SET: pins
  pinMode(BRAKE, OUTPUT);
  pinMode(REV_LEFT, OUTPUT);
  pinMode(REV_RIGHT, OUTPUT);
  digitalWrite(REV_LEFT, LOW);
  digitalWrite(REV_RIGHT, LOW);
  digitalWrite(BRAKE, HIGH);
  delay(50);

  digitalWrite(BRAKE, LOW);
  delay(300);

  //TEST: Right Turn
    Serial.println("Right Turn");
    delay(5000);
    digitalWrite(REV_LEFT, HIGH);
    digitalWrite(REV_RIGHT, HIGH);

    Serial.println("Increasing Steps");
    for(int steps=0; steps < 100; steps++){
      setThrottle(L_DAC, steps/100.0f);
      setThrottle(R_DAC, steps/100.0f);
      delay(50);
    }
    delay(5000);
    Serial.println("Decreasing Steps");
    for(int steps=0; steps < 100; steps++){
      int inv_steps = 99 - steps;
      setThrottle(L_DAC, inv_steps/100.0f);
      setThrottle(R_DAC, inv_steps/100.0f);
      delay(50);
    }

  delay(5000);

  //TEST: Reverse
    Serial.println("Reverse");
    digitalWrite(REV_LEFT, LOW);
    digitalWrite(REV_RIGHT, HIGH);

    Serial.println("Increasing Steps");
    for(int steps=0; steps <= 100; steps++){
      setThrottle(L_DAC, steps/100.0f);
      setThrottle(R_DAC, steps/100.0f);
      delay(50);
    }
    delay(5000);
    Serial.println("Decreasing Steps");
    for(int steps=0; steps < 100; steps++){
      int inv_steps = 99 - steps;
      setThrottle(L_DAC, inv_steps/100.0f);
      setThrottle(R_DAC, inv_steps/100.0f);
      delay(50);
    }

  delay(5000);

  //TEST: Left Turn
    Serial.println("Left Turn");
    digitalWrite(REV_LEFT, LOW);
    digitalWrite(REV_RIGHT, LOW);

    Serial.println("Increasing Steps");
    for(int steps=0; steps <= 100; steps++){
      setThrottle(L_DAC, steps/100.0f);
      setThrottle(R_DAC, steps/100.0f);
      delay(50);
    }
    delay(5000);
    Serial.println("Decreasing Steps");
    for(int steps=0; steps < 100; steps++){
      int inv_steps = 99 - steps;
      setThrottle(L_DAC, inv_steps/100.0f);
      setThrottle(R_DAC, inv_steps/100.0f);
      delay(50);
    }

  delay(5000);

  //TEST: Drive
    Serial.println("Drive");
    digitalWrite(REV_LEFT, HIGH);
    digitalWrite(REV_RIGHT, LOW);

    Serial.println("Increasing Steps");
    for(int steps=0; steps <= 100; steps++){
      setThrottle(L_DAC, steps/100.0f);
      setThrottle(R_DAC, steps/100.0f);
      delay(50);
    }
    delay(5000);
    Serial.println("Decreasing Steps");
    for(int steps=0; steps < 100; steps++){
      int inv_steps = 99 - steps;
      setThrottle(L_DAC, inv_steps/100.0f);
      setThrottle(R_DAC, inv_steps/100.0f);
      delay(50);
    }

  delay(5000);

  //TEST: Brake
    Serial.println("Brake Test");
    digitalWrite(BRAKE, LOW);
    //digitalWrite(REV_LEFT, HIGH); // If High Wheel would not move

    for(int steps=0; steps < 100; steps++){
      setThrottle(L_DAC, steps/100.0f);
      //setThrottle(R_DAC, steps/100.0f);
      delay(50);
    }

    // IMPORTANT: this is the brake test
    //        while throttle if active we test the brake
    digitalWrite(BRAKE, HIGH);
    delay(5000);
    digitalWrite(BRAKE, LOW);
    delay(50);
    
    for(int steps=0; steps < 100; steps++){
      int inv_steps = 99 - steps;
      setThrottle(L_DAC, inv_steps/100.0f);
      //setThrottle(R_DAC, inv_steps/100.0f);
      delay(50);
    }

  Serial.println("END");
  delay(5000);
    
}

void loop() {
  // put your main code here, to run repeatedly:
  // No loop is necessary since we are just testing once

}
