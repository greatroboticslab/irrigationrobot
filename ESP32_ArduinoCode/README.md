# Autonomous Irrigation Robot

A Wi-Fi-enabled agricultural robot that drives itself (or is driven) to a zone, positions a delta-arm-mounted tool over the target, and runs a submersible pump to water it. The system is split across **four controllers** that talk to each other almost entirely over MQTT and RS485/Modbus — this document goes through each one, how its code actually works, and exactly how the messages between them interlock.

## Table of Contents
1. [System Overview](#1-system-overview)
2. [Repository Structure](#2-repository-structure)
3. [ESP32 #1 — Drive & Pump Controller](#3-esp32-1--drive--pump-controller)  
   3.1 [Pinout & Hardware Notes](#31-pinout--hardware-notes)  
   3.2 [Arming & Control-Source Priority](#32-arming--control-source-priority)  
   3.3 [Remote-Control Path (iBus)](#33-remote-control-path-ibus)  
   3.4 [MQTT Path (Web UI)](#34-mqtt-path-web-ui)  
   3.5 [Submersible Pump Wiring](#35-submersible-pump-wiring)
4. [ESP32 #2 — Delta-Arm / Pulley Controller](#4-esp32-2--delta-arm--pulley-controller)  
   4.1 [Pinout & Hardware Notes](#41-pinout--hardware-notes)  
   4.2 [Modbus RTU Frame Construction](#42-modbus-rtu-frame-construction)  
   4.3 [Inverse Kinematics](#43-inverse-kinematics)  
   4.4 [Coordinated Move & Safety Interlock](#44-coordinated-move--safety-interlock)  
   4.5 [MQTT Command Surface](#45-mqtt-command-surface)
5. [Arduino Opta (PLC) — Axis Motion Supervisor](#5-arduino-opta-plc--axis-motion-supervisor)
6. [Raspberry Pi — Coordination, Vision & Web UI](#6-raspberry-pi--coordination-vision--web-ui)  
   6.1 [`central_script.py`](#61-central_scriptpy)  
   6.2 [`auto_navigation.py` — GPS/IMU Fusion](#62-auto_navigationpy--gpsimu-fusion)  
   6.3 [`face_tracking.py` — PID Person-Following](#63-face_trackingpy--pid-person-following)  
   6.4 [`IMU.py` — Sensor Driver](#64-imupy--sensor-driver)  
   6.5 [`GUI.py` / Flask Routes](#65-guipy--flask-routes)
7. [How It All Interlocks — Message Flow Walkthroughs](#7-how-it-all-interlocks--message-flow-walkthroughs)
8. [Motor Controller Wiring Reference](#8-motor-controller-wiring-reference)
9. [Getting Started](#9-getting-started)
10. [Known Issues / Next Steps](#10-known-issues--next-steps)

---

## 1. System Overview

| Controller | Firmware / Script | Talks to | Job |
|---|---|---|---|
| **ESP32 #1 (Drive)** | `complete_v1.ino` | RC receiver (iBus), MQTT | Drive motors, brake, gear, submersible pump |
| **ESP32 #2 (Delta/Pulley)** | `pulley_system` (.ino) | MQTT, RS485/Modbus RTU | Delta-arm inverse kinematics → axis drivers on the Opta side of the RS485 bus, plus a second submersible-pump output |
| **Arduino Opta (PLC)** | `IrrigationBot.plcprj` | RS485/Modbus (from ESP32 #2), limit switches | Homes and executes the X / Y / Delta-arm axis moves, enforces travel limits |
| **Raspberry Pi** | `central_script.py`, `GUI.py`, `auto_navigation.py`, `face_tracking.py`, `IMU.py` | Both ESP32s (MQTT), camera, GPS, moisture sensors | Web dashboard, mode/state machine, GPS+IMU autonomous navigation, face tracking, moisture-triggered irrigation logic |

```
                         ┌───────────────────────────────┐
                         │        Raspberry Pi            │
                         │  central_script.py + GUI.py     │
                         │  Flask :5000 · MQTT client       │
                         │  BerryIMU · gpsd · OpenCV         │
                         └────────────────┬────────────────┘
                                          │  MQTT broker on the Pi (192.168.1.104:1883)
              ┌────────────────────────────┼────────────────────────────┐
              │ robot/control (move string) │ robot/control ("X Y Z")    │
              │ robot/pump ("0"/"1")         │ robot/pump, robot/arm,      │
              │ robot/gear · robot/rail       │ robot/pid · robot/vision     │
              ▼                                ▼
  ┌─────────────────────────┐      ┌──────────────────────────────┐
  │  ESP32 #1 — Drive Ctrl   │      │  ESP32 #2 — Delta/Pulley Ctrl  │
  │  complete_v1.ino          │      │  pulley_system (.ino)           │
  │  iBus RC ⇄ MQTT arbiter    │      │  Delta-robot inverse kinematics  │
  │  → drive motors, brake       │      │  → RS485 Modbus RTU master        │
  │  → gear relays                │      │  → submersible pump (GPIO4)        │
  │  → submersible pump (GPIO17)   │      │  → hardwired E-stop (GPIO21)        │
  └─────────────────────────┘      └────────────────┬───────────────┘
                                                       │ RS485 (Modbus RTU, 115200 8N1)
                                                       ▼
                                      ┌──────────────────────────────┐
                                      │   Arduino Opta (PLC)            │
                                      │   IrrigationBot.plcprj            │
                                      │   Homes + executes X/Y/Delta-arm   │
                                      │   axis moves, enforces limits        │
                                      └──────────────────────────────┘
```

> The two ESP32s currently subscribe to some of the **same topic names** (`robot/control`, `robot/pump`). That's not a diagram error — it's a real overlap in the code, discussed in [§7](#7-how-it-all-interlocks--message-flow-walkthroughs) and [§10](#10-known-issues--next-steps).

## 2. Repository Structure

```
.
├── ESP32_ArduinoCode/
│   ├── complete_v1/complete_v1.ino        # ESP32 #1 — drive motors + pump (RC & MQTT)
│   └── delta_pulley_controller/           # ESP32 #2 — put pulley_system here as
│       └── delta_pulley_controller.ino    #   <folder>/<same-name>.ino for Arduino IDE
├── IrrigationBot.plcprj                   # Arduino Opta PLC project (X/Y/Delta-arm axis control)
├── central_script.py                      # Main Pi process: Flask app, MQTT client, mode/state machine
├── GUI.py                                 # Web dashboard HTML/CSS/JS (served by central_script.py)
├── auto_navigation.py                     # GPS + IMU EKF fusion, pure-pursuit path following
├── face_tracking.py                       # PID-based person-following using camera detections
├── IMU.py                                 # BerryIMU (v1/v2/v3) low-level I2C driver
├── Images/                                # Wiring & hardware reference photos
└── README.md
```

> **Note:** the uploaded `pulley_system` file has no `.ino` extension. The Arduino IDE requires a sketch's folder name to match its `.ino` filename — rename it as shown above before opening it (e.g. `delta_pulley_controller.ino` inside `delta_pulley_controller/`).

## 3. ESP32 #1 — Drive & Pump Controller

### 3.1 Pinout & Hardware Notes

```cpp
#define L_DAC       25   // Left motor throttle  (DAC / analog duty cycle)
#define R_DAC       26   // Right motor throttle (DAC / analog duty cycle)
#define REV_LEFT    32   // Left motor direction
#define REV_RIGHT   33   // Right motor direction
#define SP_HIGH     23   // Gear select — high
#define SP_LOW      22   // Gear select — low
#define BRAKE       21   // Brake

#define IBUS_RX_PIN 16   // RC receiver (Serial2, RX-only)
#define PUMP_PIN    17   // Submersible irrigation pump relay
```

Two build-specific hardware modifications matter a lot here and aren't visible from the pin `#define`s alone:

- **Ignition bypass.** These Chinese scooter/e-bike-style motor controllers normally gate everything behind a separate "ignition"/key-switch input — the controller only accepts throttle once that line is hot. On this build the ignition wire has been permanently tied to battery positive, so the controller is **always enabled**; the ESP32 no longer manages an enable line at all, it only ever needs to drive the throttle DAC pins (`L_DAC`/`R_DAC`) to make the motors move, and drop them back to `0` to stop. Practically: `setThrottle(L_DAC, 0)` is now the *only* thing standing between "parked" and "moving" — there's no separate hard power-gate downstream of the ESP32 anymore, so a stuck/garbage DAC write is the one failure mode worth testing for on the bench.
- **Shared ground requirement.** `BRAKE`, `REV_LEFT`/`REV_RIGHT`, and the throttle DAC pins are single-ended signals — the controller reads them relative to its own signal ground, not the motor power ground. **The controller's signal-ground lead must be tied to the ESP32's `GND`** or these three circuits will float and behave inconsistently (brake that won't release, reverse that won't reliably trigger, throttle that reads noise). This is separate from, and in addition to, the high-current motor power/battery ground — see [§8](#8-motor-controller-wiring-reference) for the physical connectors this applies to.

### 3.2 Arming & Control-Source Priority

The RC receiver drives two switches that gate everything: an **arm switch** (SWA) and a **control-source switch** (SWB, remote vs. web/MQTT). This means the physical transmitter is always able to override the Pi — flipping SWA disarms the robot no matter what the web UI is doing.

```cpp
int armed = readChannel(4, 0, 100, 0);   // SWA — arm/disarm
mode      = readChannel(5, 0, 100, 0);   // SWB — 0: remote, ON: web/MQTT
int speed = readChannel(6, 0, 100, 0);   // SWC — gear
int pump  = readChannel(7, 0, 100, 0);   // SWD — pump (remote mode only)

if (armed == ON) {
  if (!is_armed) { digitalWrite(BRAKE, LOW); is_armed = true; }

  if (mode == ON) {                 // Web Mode — just service MQTT
    if (!client.connected()) reconnect();
    else { client.loop(); delay(500); }
  } else {                          // Remote Mode — drive from sticks/switches directly
    ...
  }
} else if (is_armed) {              // just transitioned to disarmed
  digitalWrite(BRAKE, HIGH);
  digitalWrite(PUMP_PIN, LOW);
  setThrottle(L_DAC, 0);
  setThrottle(R_DAC, 0);
  set3Speed(0);
  is_armed = false;
}
```

### 3.3 Remote-Control Path (iBus)

In Remote Mode, the joystick's forward/turn axes are read with a small deadzone, translated into a direction on `REV_LEFT`/`REV_RIGHT`, and then a throttle duty cycle on both DAC channels (this rig turns by biasing both wheels the same direction rather than differential-speed steering):

```cpp
if (forward < 5 && forward > -5) forward = 0;
if (turn    < 10 && turn   > -10) turn    = 0;

if (turn > 0) {                 // Right turn
  digitalWrite(REV_LEFT, HIGH); digitalWrite(REV_RIGHT, HIGH);
} else if (turn < 0) {          // Left turn
  digitalWrite(REV_LEFT, LOW);  digitalWrite(REV_RIGHT, LOW);
} else if (forward > 0) {       // Forward
  digitalWrite(REV_LEFT, LOW);  digitalWrite(REV_RIGHT, HIGH);
} else {                        // Reverse
  digitalWrite(REV_LEFT, HIGH); digitalWrite(REV_RIGHT, LOW);
}

float steps = abs(forward);
setThrottle(L_DAC, steps / 100);
setThrottle(R_DAC, steps / 100);
```

`setThrottle` maps a 0–1 duty cycle onto the controller's expected voltage window (`Vmin` = idle, `Vmax` = full throttle) and writes it out over the ESP32's onboard DAC:

```cpp
void setThrottle(int dacPin, float duty01) {
  if (duty01 < 0) duty01 = 0;
  if (duty01 > 1) duty01 = 1;
  float v = Vmin + duty01 * (Vmax - Vmin);
  uint8_t code = (uint8_t)((v / VdacMax) * 255.0f);
  dacWrite(dacPin, code);
}
```

### 3.4 MQTT Path (Web UI)

In Web Mode, the same direction/throttle/brake/gear/pump outputs are instead driven by whatever arrives on `robot/control` and `robot/pump`:

```cpp
if (strcmp(topic, MQTT_MOVE_CMD) == 0) {          // "robot/control"
  char move_message[8] = {0};
  memcpy(move_message, payload, min(length, sizeof(move_message)-1));

  if      (strcmp(move_message, "0") == 0) set3Speed(0);    // gear low
  else if (strcmp(move_message, "1") == 0) set3Speed(50);   // gear mid
  else if (strcmp(move_message, "2") == 0) set3Speed(100);  // gear high

  if      (strcmp(move_message, "D") == 0) { /* forward: REV_LEFT LOW, REV_RIGHT HIGH, throttle 1 */ }
  else if (strcmp(move_message, "B") == 0) { /* backward: REV_LEFT HIGH, REV_RIGHT LOW, throttle 1 */ }
  else if (strcmp(move_message, "L") == 0) { /* left: REV_LEFT LOW, REV_RIGHT LOW, throttle 1 */ }
  else if (strcmp(move_message, "R") == 0) { /* right: REV_LEFT HIGH, REV_RIGHT HIGH, throttle 1 */ }
  else if (strcmp(move_message, "P") == 0) { digitalWrite(BRAKE, LOW); setThrottle(L_DAC, 0); setThrottle(R_DAC, 0); }
}

if (strcmp(topic, MQTT_PUMP_CMD) == 0) {          // "robot/pump"
  int PumpCmd = atoi(pump_message);
  digitalWrite(PUMP_PIN, PumpCmd == 1 ? HIGH : LOW);
}
```

These are exactly the single-character codes `central_script.py`'s Flask routes publish (`"D"`, `"B"`, `"L"`, `"R"`, `"P"`), and the `"0"`/`"1"`/`"2"` gear strings published by `/gear_low`, `/gear_mid`, `/gear_high`.

### 3.5 Submersible Pump Wiring
`PUMP_PIN` (GPIO17) drives a relay, not the pump directly — the pump itself is a **submersible** 12V DC pump sitting in the water reservoir/tank, so the relay and any exposed splices on that side of the harness should be kept outside the tank and treated as a wet-location circuit (heat-shrink every joint, route the low-voltage relay-coil side away from the submerged power leads). Logically it's simple: `robot/pump` payload `"1"` → relay closes → pump runs; `"0"` → relay opens → pump stops. See [§7](#7-how-it-all-interlocks--message-flow-walkthroughs) for why this pin isn't the only thing listening on `robot/pump`.

## 4. ESP32 #2 — Delta-Arm / Pulley Controller

This second ESP32 (`ESP32_DeltaRobot_Core` in its MQTT client ID) is the real-time brain for the suspended/pulley-driven delta arm: it takes a target (X, Y, Z) from the Pi, solves inverse kinematics for three arms spaced 120° apart, and pushes the resulting step targets out over an RS485/Modbus RTU bus toward the axis drive stage that the Opta supervises.

### 4.1 Pinout & Hardware Notes

```cpp
#define RS485_RX2_PIN   18   // UART2 RX — RS485 transceiver
#define RS485_TX2_PIN   19   // UART2 TX — RS485 transceiver
#define RS485_RTS_PIN   5    // RS485 driver-enable / flow control
#define PUMP_CTRL_PIN   4    // Submersible pump — remapped from GPIO17
#define ESTOP_PIN       21   // Active-low hardwired E-stop to the axis drivers
```

Two things worth flagging:
- **`PUMP_CTRL_PIN` was deliberately moved off GPIO17** — the in-code comment notes this avoids a PSRAM conflict/crash. On ESP32 modules that route PSRAM through GPIO16/17 (common on WROVER-class modules), repurposing those pins for GPIO can crash the board on boot; GPIO4 sidesteps that entirely.
- **`ESTOP_PIN` is active-low and wired straight to the axis drivers' hardware Quick-Stop input**, independent of any Modbus message. Disarming this ESP32 (`robot/arm` = `"0"`, or an incoming `"P"` on `robot/control`) pulls it low and halts the drivers at the hardware level, not just in software.

### 4.2 Modbus RTU Frame Construction

The board talks to three axis drivers (slave IDs 1–3) over RS485 using hand-rolled Modbus RTU frames — no external Modbus library, just a CRC-16 routine and raw byte packing:

```cpp
uint16_t calculateCRC(const uint8_t* buffer, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t pos = 0; pos < length; pos++) {
        crc ^= (uint16_t)buffer[pos];
        for (int i = 8; i != 0; i--) {
            if ((crc & 0x0001) != 0) { crc >>= 1; crc ^= 0xA001; }
            else                     { crc >>= 1; }
        }
    }
    return crc;
}
```

`sendModbusFrame` builds a standard `[slave][function][addr_hi][addr_lo][count_hi][count_lo][bytecount][data...][crc_lo][crc_hi]` frame and toggles the RS485 transceiver's `RTS` pin around the write so the bus turns around correctly:

```cpp
digitalWrite(RS485_RTS_PIN, HIGH);   // enable TX driver
ModbusSerial.write(frame, frameSize);
ModbusSerial.flush();                // block until the UART FIFO is actually drained
digitalWrite(RS485_RTS_PIN, LOW);    // back to listening
```

`setDriverTrajectory()` uses this to issue a Function Code `0x10` (Write Multiple Registers) starting at register `0x6200` — the address block this driver family uses for absolute-positioning trajectory parameters:

```cpp
void setDriverTrajectory(uint8_t slaveID, int32_t stepPosition,
                          uint16_t rpmSpeed, uint16_t accelTime, uint16_t decelTime) {
    uint16_t parameters[6];
    parameters[0] = 0x0001;                        // mode: absolute positioning
    parameters[1] = (stepPosition >> 16) & 0xFFFF;  // target position, high word
    parameters[2] = stepPosition & 0xFFFF;          // target position, low word
    parameters[3] = rpmSpeed;                       // cruise speed
    parameters[4] = accelTime;                      // accel ramp (ms)
    parameters[5] = decelTime;                      // decel ramp (ms)
    sendModbusFrame(slaveID, 0x10, 0x6200, parameters, 6);
}
```

and a Function Code `0x06` (Write Single Register) to arm the move once all three axes' trajectories are staged:

```cpp
writeModbusRegister(SLAVE_AXIS_1, 0x6002, 0x0010);  // "go"
writeModbusRegister(SLAVE_AXIS_2, 0x6002, 0x0010);
writeModbusRegister(SLAVE_AXIS_3, 0x6002, 0x0010);
```

Register `0x6000` is also written once at boot per axis to enable soft-limits:

```cpp
for (uint8_t i = 1; i <= 3; i++) {
    writeModbusRegister(i, 0x6000, 0x0002);   // enable soft-limits on each driver
}
```

### 4.3 Inverse Kinematics

The arm geometry is a classic parallel delta robot: a fixed base circle (`r_b`), a moving platform circle (`r_p`), an upper arm length (`L`), and a forearm/rod length (`l`), with three towers spaced 120° apart:

```cpp
const double r_b = 150.0;  // base radius (mm)
const double r_p = 50.0;   // platform radius (mm)
const double L   = 200.0;  // upper-arm length (mm)
const double l   = 400.0;  // forearm/rod length (mm)
```

For a requested platform point `(x0, y0, z0)`, `solveInverseKinematics` is called once per tower (0°, 120°, 240°) and returns that tower's motor angle by projecting the target into the tower's local 2D plane and solving the resulting quadratic for the elbow-down solution:

```cpp
double solveInverseKinematics(double x0, double y0, double z0, double armAngleDegrees) {
    double angleRad = armAngleDegrees * (M_PI / 180.0);
    double r_diff = r_p - r_b;

    double x_p = x0 + r_diff * cos(angleRad);
    double y_p = y0 + r_diff * sin(angleRad);

    double u = x_p * cos(angleRad) + y_p * sin(angleRad);
    double v = z0;

    double E = -2.0 * L * u;
    double F =  2.0 * L * v;
    double G =  u*u + v*v + L*L - l*l;

    double discriminant = F*F - G*G + E*E;
    if (discriminant < 0) return NAN;   // target is outside the arm's reach

    double t = (-F - sqrt(discriminant)) / (G - E);   // elbow-down root
    return 2.0 * atan(t) * (180.0 / M_PI);
}
```

Returned angles are converted to encoder/gearbox steps using a constant derived straight from the hardware's spec sheet:

```cpp
// 1000 PPR encoder (4000 CPR quadrature) × 46.656 gearbox = 186,624 steps/rev
// 186,624 steps / 360° = 518.4 steps/degree
const double STEPS_PER_DEGREE = 518.4;
```

### 4.4 Coordinated Move & Safety Interlock

`executeCoordinatedMove` ties the above together and is the single choke point all motion goes through — including the vision-tracking closed loop in §4.5. It refuses to move at all unless the board has been armed over MQTT, and refuses to move if *any* of the three towers' kinematics come back unreachable (all-or-nothing, so the platform never partially commits to an invalid pose):

```cpp
void executeCoordinatedMove(double x0, double y0, double z0) {
    if (!isArmed) return;

    double theta1 = solveInverseKinematics(x0, y0, z0, 0.0);
    double theta2 = solveInverseKinematics(x0, y0, z0, 120.0);
    double theta3 = solveInverseKinematics(x0, y0, z0, 240.0);
    if (isnan(theta1) || isnan(theta2) || isnan(theta3)) return;

    int32_t s1 = theta1 * STEPS_PER_DEGREE;
    int32_t s2 = theta2 * STEPS_PER_DEGREE;
    int32_t s3 = theta3 * STEPS_PER_DEGREE;

    setDriverTrajectory(SLAVE_AXIS_1, s1, 180, 40, 40);
    setDriverTrajectory(SLAVE_AXIS_2, s2, 180, 40, 40);
    setDriverTrajectory(SLAVE_AXIS_3, s3, 180, 40, 40);

    writeModbusRegister(SLAVE_AXIS_1, 0x6002, 0x0010);
    writeModbusRegister(SLAVE_AXIS_2, 0x6002, 0x0010);
    writeModbusRegister(SLAVE_AXIS_3, 0x6002, 0x0010);

    currentX = x0; currentY = y0; currentZ = z0;   // remember where we ended up
}
```

Arming/disarming is the same GPIO that backs the hardwired `ESTOP_PIN`, so software-disarm and the physical E-stop input converge on one code path:

```cpp
void updateArmingState(bool armState) {
    isArmed = armState;
    if (isArmed) {
        digitalWrite(ESTOP_PIN, HIGH);      // release the drivers' Quick-Stop
    } else {
        digitalWrite(ESTOP_PIN, LOW);       // hard-stop the drivers
        digitalWrite(PUMP_CTRL_PIN, LOW);   // and kill the pump on this board too
    }
}
```

### 4.5 MQTT Command Surface

```cpp
mqttClient.subscribe("robot/control");  // "X Y Z" → move, or "P" → E-stop
mqttClient.subscribe("robot/pump");     // "1"/"0" → this board's pump relay
mqttClient.subscribe("robot/arm");      // "1"/"0" → arm/disarm the delta stage
mqttClient.subscribe("robot/pid");      // "Kp Ki Kd" → live PID tuning
mqttClient.subscribe("robot/vision");   // "area offset" → camera-tracking feedback
```

The vision path is a small closed loop entirely on this board: it takes a face/target's pixel-center offset, applies proportional gain, and nudges the platform's X coordinate to re-center it — the same idea as `face_tracking.py`'s PID loop, just applied to the delta arm instead of the whole rover:

```cpp
} else if (strcmp(topic, "robot/vision") == 0) {
    double areaVal, offsetVal;
    if (sscanf(messageBuffer, "%lf %lf", &areaVal, &offsetVal) == 2) {
        desiredFaceArea = areaVal;
        centerOffset = offsetVal;

        double error = centerOffset;
        double correction = Kp * error;
        double newX = currentX + correction;
        executeCoordinatedMove(newX, currentY, currentZ);
    }
}
```

## 5. Arduino Opta (PLC) — Axis Motion Supervisor

`IrrigationBot.plcprj` is a CODESYS-style project targeting an **Arduino Opta**, running `Fast` (10 ms) / `Slow` (100 ms) / `Background` (500 ms) cyclic tasks. Its holding-register map is the supervisory layer for the same X/Y/Delta-arm axes ESP32 #2 is driving over Modbus — home/limit-switch handling and start/done handshaking live here rather than on the ESP32:

| Register (mnemonic) | Purpose |
|---|---|
| `hrCmd` | Command word |
| `hrXTarget`, `hrYTarget`, `hrDTarget` | Target position per axis (steps) |
| `hrSpeed`, `hrAccel` | Motion profile (steps/sec, steps/sec²) handed to `AccelStepper` |
| `hrAxisMask` | Bitmask selecting axes for relative moves |
| *(status word)* | Machine status |
| *(X/Y/D position, error code, completion pulse)* | Live feedback back to the supervisor |
| *X/Y home & over-travel, Delta-arm up/home & fully-extended* | Digital limit-switch inputs (wired NC for fail-safe behavior) |

The handshake pattern is explicit in the variable descriptions: **"PLC sets TRUE → Sketch starts move"**, **"Sketch sets TRUE → move done"**, and a **"Sketch stops all motors NOW"** coil the PLC can assert at any time — i.e. the Opta side owns homing safety and start/complete sequencing, while the actual step generation happens in an `AccelStepper`-based sketch it hands targets to.

> The Opta's register *names* (`hrXTarget`, `hrSpeed`, …) and ESP32 #2's raw Modbus register *addresses* (`0x6200`, `0x6002`, `0x6000`) are two different views into the same motion pipeline rather than the same register bank — treat this as the one seam in the stack most worth bench-verifying end-to-end (see [§10](#10-known-issues--next-steps)).

## 6. Raspberry Pi — Coordination, Vision & Web UI

### 6.1 `central_script.py`
Boots the MQTT client, IMU, and `gpsd` connection; starts the Flask app and the `face_tracking_process`; and runs a `main_loop` thread that composites the camera feed, overlays mode/heading/E-stop status, and routes commands based on `current_movement_mode`:

```python
if current_movement_mode == 'face_tracking':
    if not detection_queue.empty():
        detection = detection_queue.get()
        command_queue.put(('face_tracking', detection))
    else:
        client.publish(MQTT_TOPIC_COMMAND, "64 64")   # no target: stop
elif current_movement_mode == 'auto_navigation':
    pass  # handled entirely by auto_navigation_process
elif current_movement_mode == 'basic_movement':
    pass  # manual Flask routes drive this directly
```

Moisture-triggered irrigation lives in the MQTT `on_message` handler — a reading below `set_threshold` for a MAC address mapped to a zone turns the pump on; recovering above threshold turns it back off:

```python
if current_pump_mode == AUTO_MODE and zone:
    if value < set_threshold and pump_cmd == "0":
        pump_cmd = "1"
        client.publish(MQTT_TOPIC_PUMP, pump_cmd)
        pump_states[zone] = True
    elif value >= set_threshold and pump_cmd == "1":
        pump_cmd = "0"
        client.publish(MQTT_TOPIC_PUMP, pump_cmd)
        pump_states[zone] = False
```

### 6.2 `auto_navigation.py` — GPS/IMU Fusion
Runs as a separate process. Converts GPS lat/lon into a local UTM frame (`pyproj`), then fuses GPS position with IMU accelerometer/magnetometer readings using a 7-state Extended Kalman Filter — state vector `[x, y, vx, vy, ax, ay, θ]`:

```python
def ekf_predict(x_est, P_est, accel_data):
    F = np.array([
        [1, 0, dt, 0, 0.5*dt**2, 0, 0],
        [0, 1, 0, dt, 0, 0.5*dt**2, 0],
        [0, 0, 1, 0, dt, 0, 0],
        [0, 0, 0, 1, 0, dt, 0],
        [0, 0, 0, 0, 1, 0, 0],
        [0, 0, 0, 0, 0, 1, 0],
        [0, 0, 0, 0, 0, 0, 1],
    ])
    u = np.array([[0],[0],[0],[0],[accel_data[0]],[accel_data[1]],[0]])
    x_pred = F @ x_est + u * dt
    P_pred = F @ P_est @ F.T + Q
    return x_pred, P_pred
```

Waypoints (drawn on the dashboard map) are followed with a look-ahead/pure-pursuit goal-point search — `find_goal_point` walks the path segments looking for where a circle of radius `look_ahead_distance` around the robot intersects the route, and `find_min_angle` turns that into a signed heading correction:

```python
def find_goal_point(path, current_pos, look_ahead_distance, last_found_index):
    for i in range(last_found_index, len(path) - 1):
        intersections = line_circle_intersection(current_pos, path[i], path[i+1], look_ahead_distance)
        if intersections:
            goal_point = min(intersections, key=lambda pt: pt_to_pt_distance(pt, path[i+1]))
            return goal_point, i
    return path[-1], len(path) - 1
```

### 6.3 `face_tracking.py` — PID Person-Following
Runs as a separate process. Two independent PID loops — yaw from the detection's horizontal pixel offset, forward/back from how the detection's bounding-box area compares to a target size — get mapped into the same `"<front_back> <side_side>"` command format ESP32 #1 expects:

```python
error_yaw = x - center_x
speed_yaw = (pid_yaw[0]*error_yaw) + (pid_yaw[1]*iError_yaw) + (pid_yaw[2]*dError_yaw)
speed_yaw = np.clip(speed_yaw, -max_speed_yaw, max_speed_yaw)

error_fb = desired_face_area - area
speed_fb = (pid_fb[0]*error_fb) + (pid_fb[1]*iError_fb) + (pid_fb[2]*dError_fb)
speed_fb = np.clip(speed_fb, -max_speed_fb, max_speed_fb)

side_side_command  = int(64 + (speed_yaw/max_speed_yaw) * 62)   # 0–126, 64 = center
front_back_command = int(64 + (speed_fb/max_speed_fb) * 62)     # 0–126, 64 = stop
command_string = f"{front_back_command} {side_side_command}"
```

Target face size and frame-center offset are live-tunable from the dashboard (`/increase_face_area`, `/decrease_face_area`, `/move_center_left`, `/move_center_right`), all delivered through the shared `command_queue`.

### 6.4 `IMU.py` — Sensor Driver
Auto-detects a BerryIMU v1 (LSM9DS0), v2 (LSM9DS1), or v3 (LSM6DSL + LIS3MDL) over I2C by probing each chip's WHO_AM_I register, then exposes raw accelerometer/gyroscope/magnetometer reads used by both `central_script.py` (heading overlay) and `auto_navigation.py` (EKF input):

```python
def detectIMU():
    global BerryIMUversion
    try:
        g = bus.read_byte_data(LSM9DS1_GYR_ADDRESS, LSM9DS1_WHO_AM_I_XG)
        m = bus.read_byte_data(LSM9DS1_MAG_ADDRESS, LSM9DS1_WHO_AM_I_M)
    except IOError:
        pass
    else:
        if g == 0x68 and m == 0x3d:
            BerryIMUversion = 2
    ...
```

### 6.5 `GUI.py` / Flask Routes
`GUI.py` returns the dashboard's HTML/CSS/JS (Leaflet map + drawing tools, live MJPEG feed, mode selector, movement/gear/pump controls, PID tuning, moisture threshold slider), rendered by `central_script.py`'s `/` route. Every button on that page maps 1:1 to a Flask route, which maps 1:1 to either an MQTT publish or a local `command_queue` push:

| Route | Publishes / Does |
|---|---|
| `/move_forward` `/move_backward` `/move_left` `/move_right` `/stop_robot` | `robot/control` = `"D"`/`"B"`/`"L"`/`"R"`/`"P"` |
| `/gear_low` `/gear_mid` `/gear_high` | `robot/gear` = `"0"`/`"1"`/`"2"` |
| `/pump_on` `/pump_off` | `robot/pump` = `"1"`/`"0"` (also flips to `MANUAL_MODE`) |
| `/pump_auto` | Flips `current_pump_mode` to `AUTO_MODE` (moisture-triggered, see §6.1) |
| `/update_MoistThreshold` | Sets `set_threshold` locally |
| `/send_coordinates` | `command_queue.put(('set_waypoints', coordinates))` → `auto_navigation.py` |
| `/set_mode` | Sets `current_movement_mode`; spawns/stops the `auto_navigation_process` |
| `/update_pid` | `command_queue.put(('update_pid', (kp, ki, kd)))` → **local** `face_tracking_process` only |
| `/increase_face_area` `/decrease_face_area` `/move_center_left` `/move_center_right` | Local `command_queue` pushes → `face_tracking_process` |
| `/move_rail_forward` `/move_rail_backward` `/stop_rail` | `robot/rail` = `0`/`126`/`64` |
| `/estop` `/undo_estop` | Sets `e_stop_active`; `/estop` also publishes `robot/control` = `"64 64"` |

## 7. How It All Interlocks — Message Flow Walkthroughs

**A. Manual drive from the dashboard.** `/move_forward` → `client.publish("robot/control", "D")`. **Both** ESP32s receive it. ESP32 #1 matches `"D"` against its direction `strcmp`s and drives forward. ESP32 #2's handler tries `sscanf(msg, "%lf %lf %lf", ...)` first — that fails on a single letter, so it falls through to `strcmp(msg, "P")`, which also fails — net effect, harmless no-op on the delta board. **The one exception is Stop:** `/stop_robot` publishes `"P"`, which ESP32 #1 reads as brake-and-zero-throttle, but ESP32 #2 reads it as its **emergency-stop code** and calls `updateArmingState(false)` — disarming the delta arm and killing its pump output as a side effect of the rover simply stopping. Worth knowing before you rely on "Stop" during a delta-arm operation.

**B. Auto-navigation.** Drawn waypoints → `/send_coordinates` → `auto_navigation_process` → EKF-fused position → pure-pursuit heading correction → (once wired up, see §10) `robot/control` drive commands, same path as (A).

**C. Face tracking (rover body).** Camera detections → `detection_queue` → `face_tracking_process` → PID → `robot/control` = `"<front_back> <side_side>"` → ESP32 #1 turns/advances the whole rover to keep the face centered.

**D. Face tracking (delta arm / camera gimbal).** ESP32 #2's firmware is *ready* for this — `robot/vision` (area + offset) and `robot/pid` (live gains) are both subscribed and wired into `executeCoordinatedMove`. Nothing in `central_script.py`/`GUI.py` currently publishes to those topics, though; today's face-tracking output only ever reaches ESP32 #1. Publishing the same detection data the Pi already has onto `robot/vision` is the missing link to make the delta arm track a face independently of the rover's body (see §10).

**E. Submersible pump.** `/pump_on` / `/pump_off` / moisture-auto-trigger all publish to the single `robot/pump` topic. **Both** ESP32s act on it — ESP32 #1's `PUMP_PIN` (GPIO17) and ESP32 #2's `PUMP_CTRL_PIN` (GPIO4). If only one relay is physically wired to the pump, only that board's GPIO matters in practice; if both are wired (e.g. to two separate pumps, or redundantly to one), they'll switch in lockstep since they're driven by the same broadcast message.

## 8. Motor Controller Wiring Reference

Photos of the labeled connector harness on the ESP32 #1 drive motor controller — useful when re-wiring or replacing a damaged connector. Labels are physically written on the housings (English/Chinese) for **Throttle**, **The Brake**, **Reverse**, and **3-Speed**, corresponding to `setThrottle()`/`L_DAC`+`R_DAC`, `BRAKE` (GPIO21), `REV_LEFT`/`REV_RIGHT` (GPIO32/33), and `SP_HIGH`/`SP_LOW` (GPIO23/22) respectively in `complete_v1.ino`. Per §3.1: the controller's ignition line is permanently bridged hot, and each of Brake/Throttle/Reverse needs its signal ground tied back to the ESP32's `GND` to read cleanly.

| Photo | What it shows |
|---|---|
| ![Harness overview](Images/harness_overview.jpeg) | Full connector harness coming off the controller/motor mount |
| ![Speed & Reverse connectors](Images/speed_reverse_connectors.jpeg) | "3 Speed" and "Reverse" connectors |
| ![Brake & Throttle connectors](Images/brake_throttle_connectors.jpeg) | "The Brake" and "Throttle" connectors, with signal/ground leads |
| ![Hall sensor lead](Images/hall_sensor_lead.jpeg) | Motor hall-sensor lead prior to connectorization |
| ![Hall sensor connector](Images/hall_sensor_connector.jpeg) | 6-pin hall sensor connector (motor phase feedback) |
| ![Throttle lead label](Images/throttle_lead_label.jpeg) | Close-up of the throttle lead label |
| ![Throttle / 3-Speed labels](Images/throttle_3speed_labels.jpeg) | Throttle and 3-Speed connectors side by side |
| ![Reverse / Brake / Throttle labels](Images/reverse_brake_throttle_labels.jpeg) | All three labeled connectors together for reference |
| ![ESP32 breadboard](Images/esp32_breadboard.jpeg) | ESP32 dev module bench-prototyped with the iBus receiver header and terminal blocks before final install |

> ⚠️ Always verify wire color/label against the physical controller before reconnecting — colors can vary between controller batches even when labels match.

## 9. Getting Started

**ESP32 #1 (Drive):**
1. Open `ESP32_ArduinoCode/complete_v1/complete_v1.ino`. Board: **ESP32 Dev Module** (core v2.0.6). Install **PubSubClient** and **IBusBM**.
2. Set `ssid`/`password` and `mqtt_server` to match your network / Pi IP.
3. Flash, then power from its own dedicated battery — separate from the logic/pump battery.

**ESP32 #2 (Delta/Pulley):**
1. Rename `pulley_system` → `delta_pulley_controller.ino` inside a `delta_pulley_controller/` folder. Install **PubSubClient** (WiFi/HardwareSerial/math are core libraries).
2. Set `WIFI_SSID`/`WIFI_PASSWORD` and `MQTT_BROKER` to match.
3. Wire the RS485 transceiver's RO/DI/RE/DE to GPIO18/19/5 and confirm the axis drivers are on slave IDs 1–3 at 115200 8N1.
4. Flash. The board boots disarmed (`updateArmingState(false)`) — you must publish `robot/arm` = `"1"` before it will act on any `robot/control` coordinate.

**Arduino Opta (PLC):**
1. Open `IrrigationBot.plcprj` in your CODESYS-based Opta toolchain.
2. Verify home/limit-switch wiring for X, Y, and the Delta-arm axis before the first homing cycle.

**Raspberry Pi:**
1. Install `flask`, `opencv-python`, `paho-mqtt`, `numpy`, `pyproj`, `gpsd-py3`, `smbus`.
2. Run an MQTT broker (e.g. Mosquitto) matching `MQTT_SERVER` in `central_script.py`.
3. Confirm the BerryIMU is on I2C and `gpsd` is running against your GPS module.
4. `python3 central_script.py`, then browse to `http://<robot_ip>:5000`.

## 10. Known Issues / Next Steps

- **`robot/control` and `robot/pump` are shared between both ESP32s.** In practice this mostly falls through harmlessly (§7-A), but the `"P"` stop code doubles as ESP32 #2's E-stop trigger — sending "Stop" from the dashboard will disarm the delta arm as a side effect. Worth splitting into namespaced topics (e.g. `robot/drive/control` vs. `robot/arm/control`) so the two boards can't cross-talk at all.
- **The delta arm's vision/PID closed loop is unreachable from the Pi today.** ESP32 #2 subscribes to `robot/vision`, `robot/pid`, and `robot/arm`, but nothing in `central_script.py` or `GUI.py` currently publishes to them — `/update_pid` and the face-area/center-offset routes only reach the local `face_tracking_process`, which drives the rover body, not the arm.
- **Opta register map vs. ESP32 #2's raw Modbus addresses haven't been cross-verified end-to-end.** `hrXTarget`/`hrSpeed`/etc. (Opta) and `0x6200`/`0x6002`/`0x6000` (ESP32 #2) are almost certainly two views of the same motion pipeline, but that hasn't been confirmed on the bench — worth a dedicated smoke test before relying on it.
- **Dual pump control.** Both ESP32s carry a pump GPIO reacting to the same `robot/pump` message (§7-E) — confirm which one (or both) is actually wired to the submersible pump's relay to avoid surprises.
- **`auto_navigation_process`'s follow-loop is stubbed.** Waypoints are received and the EKF is initialized, but the pure-pursuit → `robot/control` command loop (`track_robot`) still needs to be implemented.
- **Zone B/C moisture sensor MACs are placeholders** (`AA:AA:AA:AA:AA:AA`) in `central_script.py` — swap in real sensor MACs before relying on auto-irrigation for those zones.
- **Path memory** (recording a driven route as a repeatable patrol) is planned but not yet implemented.
