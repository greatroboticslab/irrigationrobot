This integration guide provides the firmware logic for your ESP32 Master, allowing it to communicate with the Arduino Opta Slave over RS485.

To implement this, you will need the **ModbusMaster** library installed in your Arduino IDE (Library Manager → Search for "ModbusMaster" by Doc Walker).

### 1. Hardware Connection (Physical Layer)

You will need an RS485 transceiver module (like a MAX485 or similar 3.3V module).

| ESP32 Pin | RS485 Module Pin | Note |
| --- | --- | --- |
| **GPIO 16 (RX2)** | RO (Receiver Out) |  |
| **GPIO 17 (TX2)** | DI (Driver In) |  |
| **GPIO 4** | DE & RE (Tie together) | Logic HIGH to transmit, LOW to receive |
| **GND** | GND | **Crucial:** Must share GND with Opta |
| **5V** | VCC | Check module voltage requirements |

*Connect the RS485 `A` and `B` terminals to the corresponding `A` and `B` terminals on the Arduino Opta RS485 port.*

---

### 2. ESP32 Implementation (`Master_Controller.ino`)

```cpp
#include <ModbusMaster.h>

// ── Pin Config ─────────────────────────────────────────────
#define MAX485_DE_RE 4  // Connect to DE and RE pins
#define RX_PIN 16
#define TX_PIN 17

// ── Modbus Setup ───────────────────────────────────────────
ModbusMaster node;

// Callback: Enable Driver (High) before sending, Disable (Low) after
void preTransmission() {
  digitalWrite(MAX485_DE_RE, HIGH);
}
void postTransmission() {
  digitalWrite(MAX485_DE_RE, LOW);
}

void setup() {
  pinMode(MAX485_DE_RE, OUTPUT);
  digitalWrite(MAX485_DE_RE, LOW); // Default to receive

  Serial.begin(115200);      // Debugging
  Serial2.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN); // Modbus bus

  node.begin(1, Serial2); // Slave address 1
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
  
  Serial.println("ESP32 Master Initialized.");
}

// ── Helper: Wait for Completion ────────────────────────────
bool waitForDone() {
  uint32_t startWait = millis();
  while (millis() - startWait < 5000) { // 5s timeout
    uint8_t result = node.readInputRegisters(0x0000, 6); // Read 30001-30006
    if (result == node.ku8MBSuccess) {
      // Check Done Flag (30006 is index 5)
      if (node.getResponseBuffer(5) == 1) return true;
      // Check Error (30005 is index 4)
      if (node.getResponseBuffer(4) > 0) {
        Serial.print("Error detected! Code: ");
        Serial.println(node.getResponseBuffer(4));
        return false;
      }
    }
    delay(50);
  }
  return false; // Timeout
}

// ── Helper: Send Command ───────────────────────────────────
void sendCommand(uint16_t cmd, int16_t x, int16_t y, int16_t d, uint16_t speed) {
  node.writeSingleRegister(0x0000, cmd);    // 40001
  node.writeSingleRegister(0x0001, x);      // 40002
  node.writeSingleRegister(0x0002, y);      // 40003
  node.writeSingleRegister(0x0003, d);      // 40004
  node.writeSingleRegister(0x0004, speed);  // 40005
  // Note: Add accel/mask logic here if needed
  Serial.printf("Sent CMD: %d\n", cmd);
}

// ── Main Loop: Pick Sequence ───────────────────────────────
void loop() {
  // 1. HOME ALL (Run at start)
  static bool homed = false;
  if (!homed) {
    sendCommand(3, 0, 0, 0, 400); // 3=HomeAll
    if (waitForDone()) {
      Serial.println("Homing complete.");
      homed = true;
    }
  }

  // 2. MOVE TO STRAWBERRY (Logic here)
  int x_target = 1000; // Example target
  int y_target = 2000;
  
  sendCommand(1, x_target, y_target, 0, 800); // 1=MoveAbs
  if (waitForDone()) {
    
    // 3. LOWER DELTA
    sendCommand(4, 0, 0, 500, 300); // 4=DeltaOnly
    waitForDone();
    
    // Perform Vacuum Trigger here...
    delay(500); 
    
    // 4. RETRACT DELTA
    sendCommand(4, 0, 0, 0, 300);
    waitForDone();
    
    Serial.println("Pick successful.");
  }

  delay(2000); // Wait before next pick
}

```

### 3. Integration Tips for the ESP32

* **Modbus Addressing:** In the `ModbusMaster` library, register addresses are **offsets**.
* Register `40001` in the PLC corresponds to address `0x0000` in the library.
* Register `30006` corresponds to index `5` in the input register array.


* **Non-Blocking Logic:** The example code uses a `waitForDone()` helper that blocks. In a production environment with vision processing running on the same ESP32, replace `waitForDone()` with a state machine pattern to ensure the vision code isn't halted while waiting for motion.
* **The "Clear Fault" Sequence:** If your `waitForDone()` returns `false` due to an error, you must:
1. Read the error code from register `30005`.
2. Check the state (e.g., limit switches).
3. Send `sendCommand(9, 0, 0, 0, 0)` to reset the PLC `MachineState` to `ST_IDLE`.



---

