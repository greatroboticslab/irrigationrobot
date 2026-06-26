# Strawberry Harvester — Arduino Opta PLC Program
**Platform:** Arduino Opta + Arduino PLC IDE  
**Language:** IEC 61131-3 Structured Text (ST)  
**Communication:** Modbus RTU over RS485 (ESP32 = Master, Opta = Slave)  
**Step Generation:** AccelStepper in the Sketch layer (timing-critical)

---

## 1. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│  ESP32 (Modbus RTU Master)                                          │
│  - Vision / decision logic                                          │
│  - Writes: command, X/Y/Delta targets, speed                       │
│  - Reads: status, current position, done flag                      │
└──────────────────────────┬──────────────────────────────────────────┘
                           │ RS485 (Modbus RTU, 115200 baud)
┌──────────────────────────▼──────────────────────────────────────────┐
│  Arduino Opta PLC                                                   │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  PLC Program (Structured Text) — 10ms scan cycle           │   │
│  │  • Modbus register handling                                 │   │
│  │  • State machine (IDLE / MOVING / HOMING / ERROR / ESTOP)  │   │
│  │  • Safety: limit switch monitoring                          │   │
│  │  • Writes target variables → Sketch layer                  │   │
│  └──────────────────────────┬──────────────────────────────────┘   │
│                             │ Shared global variables               │
│  ┌──────────────────────────▼──────────────────────────────────┐   │
│  │  Arduino Sketch — runs loop() at full speed (~1MHz)         │   │
│  │  • AccelStepper for X, Y, Delta                             │   │
│  │  • Reads target variables from PLC layer                    │   │
│  │  • Generates STEP / DIR pulses (up to ~10,000 steps/sec)   │   │
│  │  • Reports current position back to PLC layer               │   │
│  └──────────────────────────┬──────────────────────────────────┘   │
│                             │ Digital I/O                           │
└─────────────────────────────┼───────────────────────────────────────┘
                              │
        ┌─────────────────────┼──────────────────────┐
        ▼                     ▼                        ▼
  X Stepper Driver      Y Stepper Driver         Delta Driver
  (STEP/DIR/EN)         (STEP/DIR/EN)            (STEP/DIR/EN)
        │                     │                        │
   X Motor               Y Motor               Delta Motor
  (Outer Rail)         (Cross Beam)            (Delta Arm)
```

**Why split PLC + Sketch?**  
The Opta PLC scan cycle (10ms) is too slow to generate step pulses above ~50Hz directly.
AccelStepper in the Sketch layer runs uninterrupted and can do 10,000+ steps/sec with
proper acceleration curves. The PLC focuses on logic, safety, and Modbus.

---

## 2. Modbus RTU Register Map

Configure in Arduino PLC IDE under **Fieldbus → Modbus RTU Slave**  
Slave address: **1** | Baud: **115200** | Parity: **None** | Stop bits: **1**

### Holding Registers — ESP32 **writes**, Opta reads
| Address | Name            | Type | Range      | Description                                      |
|---------|-----------------|------|------------|--------------------------------------------------|
| 40001   | CMD_REGISTER    | UINT | 0–9        | 0=Idle, 1=MoveAbs, 2=MoveRel, 3=HomeAll, 4=DeltaOnly, 5=EStop, 9=ClearFault |
| 40002   | CMD_X_TARGET    | INT  | ±32767     | X target (steps). Absolute or relative per CMD. |
| 40003   | CMD_Y_TARGET    | INT  | ±32767     | Y target (steps).                               |
| 40004   | CMD_D_TARGET    | INT  | ±32767     | Delta arm target (steps). 0 = home/up.          |
| 40005   | CMD_SPEED       | UINT | 1–5000     | Max speed in steps/sec                          |
| 40006   | CMD_ACCEL       | UINT | 1–2000     | Acceleration in steps/sec²                      |
| 40007   | CMD_AXIS_MASK   | UINT | 0–7        | Bitmask: bit0=X, bit1=Y, bit2=Delta (for MoveRel) |

### Input Registers — Opta **writes**, ESP32 reads
| Address | Name            | Type | Description                                      |
|---------|-----------------|------|--------------------------------------------------|
| 30001   | STATUS_REG      | UINT | 0=Idle, 1=Moving, 2=Homing, 3=Error, 4=EStopped |
| 30002   | CURRENT_X       | INT  | Current X position in steps                      |
| 30003   | CURRENT_Y       | INT  | Current Y position in steps                      |
| 30004   | CURRENT_D       | INT  | Current Delta position in steps                  |
| 30005   | ERROR_CODE      | UINT | 0=None, 1=X_Limit, 2=Y_Limit, 3=D_Limit, 4=Comm |
| 30006   | DONE_FLAG       | UINT | Pulses to 1 for ~300ms when command completes    |

---

## 3. Pin Assignment

### Opta Built-in I/O (via PLC IDE I/O configurator)
| Terminal | PLC Tag      | Function                  |
|----------|-------------|---------------------------|
| I1       | LIM_X_MIN   | X axis home limit switch  |
| I2       | LIM_X_MAX   | X axis max/over-travel    |
| I3       | LIM_Y_MIN   | Y axis home limit switch  |
| I4       | LIM_Y_MAX   | Y axis max/over-travel    |
| I5       | LIM_D_MIN   | Delta arm up/home limit   |
| I6       | LIM_D_MAX   | Delta arm down limit      |
| O1 (Relay)| EN_ALL     | Master enable to all drivers (energize = enabled) |

### Expansion Module / Sketch-accessible Pins (for STEP/DIR)
Use **Arduino Opta Digital Expansion (AFX00005)** or access STM32 pins in the Sketch.
Map these in your sketch — adjust pin numbers to match your expansion module wiring:

| Signal  | Suggested Pin | Note                              |
|---------|--------------|-----------------------------------|
| X_STEP  | D0 / EXP_O1  | Pulse per step                    |
| X_DIR   | D1 / EXP_O2  | HIGH = positive direction         |
| X_EN    | D2 / EXP_O3  | LOW = enabled (most drivers)      |
| Y_STEP  | D3 / EXP_O4  | Pulse per step                    |
| Y_DIR   | D4 / EXP_O5  | HIGH = positive direction         |
| Y_EN    | D5 / EXP_O6  | LOW = enabled                     |
| D_STEP  | D6 / EXP_O7  | Pulse per step                    |
| D_DIR   | D7 / EXP_O8  | HIGH = down toward strawberry     |
| D_EN    | D8            | LOW = enabled                     |

---

## 4. Global Variable List — `GVL.var`

Declare this as your Global Variable List in the PLC IDE project.
Variables prefixed `sk_` are **shared with the Arduino Sketch** — declare them
as `{attribute 'external'}` or map them in the IDE's shared variable panel.

```pascal
VAR_GLOBAL

    // ═══════════════════════════════════════════════════════
    // MODBUS HOLDING REGISTERS — written by ESP32, read here
    // Map these in the Modbus Slave configurator as HR 40001..40007
    // ═══════════════════════════════════════════════════════
    hrCmd           : UINT  := 0;   // Command register
    hrXTarget       : INT   := 0;   // X target position (steps)
    hrYTarget       : INT   := 0;   // Y target position (steps)
    hrDTarget       : INT   := 0;   // Delta target position (steps)
    hrSpeed         : UINT  := 400; // Steps/sec
    hrAccel         : UINT  := 100; // Steps/sec²
    hrAxisMask      : UINT  := 3;   // Bitmask for relative moves

    // ═══════════════════════════════════════════════════════
    // MODBUS INPUT REGISTERS — written here, read by ESP32
    // Map as IR 30001..30006
    // ═══════════════════════════════════════════════════════
    irStatus        : UINT  := 0;   // Machine status
    irXPos          : INT   := 0;   // Current X position
    irYPos          : INT   := 0;   // Current Y position
    irDPos          : INT   := 0;   // Current Delta position
    irErrorCode     : UINT  := 0;   // Error code
    irDoneFlag      : UINT  := 0;   // Completion pulse flag

    // ═══════════════════════════════════════════════════════
    // LIMIT SWITCH INPUTS — map to Opta terminals I1..I6
    // ═══════════════════════════════════════════════════════
    LIM_X_MIN       : BOOL  := FALSE;  // X home/min limit (NC wired = safer)
    LIM_X_MAX       : BOOL  := FALSE;  // X over-travel limit
    LIM_Y_MIN       : BOOL  := FALSE;  // Y home/min limit
    LIM_Y_MAX       : BOOL  := FALSE;  // Y over-travel limit
    LIM_D_MIN       : BOOL  := FALSE;  // Delta arm up/home limit
    LIM_D_MAX       : BOOL  := FALSE;  // Delta arm fully extended limit

    // ═══════════════════════════════════════════════════════
    // SKETCH INTERFACE — shared between PLC and Sketch layer
    // Declare these as 'external' in the sketch
    // ═══════════════════════════════════════════════════════
    sk_X_Target         : DINT  := 0;      // Target X for AccelStepper
    sk_Y_Target         : DINT  := 0;      // Target Y for AccelStepper
    sk_D_Target         : DINT  := 0;      // Target Delta for AccelStepper
    sk_Speed            : UDINT := 400;    // Speed handed to AccelStepper
    sk_Accel            : UDINT := 100;    // Accel handed to AccelStepper
    sk_ExecuteMove      : BOOL  := FALSE;  // PLC sets TRUE → Sketch starts move
    sk_MoveComplete     : BOOL  := FALSE;  // Sketch sets TRUE → move done
    sk_HomeCmd          : BOOL  := FALSE;  // PLC sets TRUE → Sketch homes all axes
    sk_HomeComplete     : BOOL  := FALSE;  // Sketch sets TRUE → homing done
    sk_EStop            : BOOL  := FALSE;  // TRUE → Sketch stops all motors NOW
    sk_CurrentX         : DINT  := 0;      // Live X position from AccelStepper
    sk_CurrentY         : DINT  := 0;      // Live Y position from AccelStepper
    sk_CurrentD         : DINT  := 0;      // Live Delta position from AccelStepper
    sk_Error            : UINT  := 0;      // Error code from Sketch (0=OK)

    // Limit switch mirror for Sketch (homing needs to see them)
    sk_LIM_X_MIN        : BOOL  := FALSE;
    sk_LIM_Y_MIN        : BOOL  := FALSE;
    sk_LIM_D_MIN        : BOOL  := FALSE;

    // ═══════════════════════════════════════════════════════
    // INTERNAL STATE MACHINE — do not modify externally
    // ═══════════════════════════════════════════════════════
    MachineState    : UINT  := 0;   // Current state
    PrevCmd         : UINT  := 0;   // Previous command for edge detection

    // State constants (use these names in your code for readability)
    ST_IDLE         : UINT  := 0;
    ST_MOVING       : UINT  := 1;
    ST_HOMING       : UINT  := 2;
    ST_WAIT_DONE    : UINT  := 5;
    ST_ERROR        : UINT  := 4;
    ST_ESTOPPED     : UINT  := 3;

    // Error code constants
    ERR_NONE        : UINT  := 0;
    ERR_X_LIMIT     : UINT  := 1;
    ERR_Y_LIMIT     : UINT  := 2;
    ERR_D_LIMIT     : UINT  := 3;
    ERR_COMM        : UINT  := 4;
    ERR_SKETCH      : UINT  := 5;

END_VAR
```

---

## 5. Main PLC Program — `Main.st`

Set your scan cycle to **10ms** in the task configuration.

```pascal
// ════════════════════════════════════════════════════════════════════
// PROGRAM Main
// Strawberry Harvester — Gantry + Delta Motion Controller
// Scan cycle: 10ms
// ════════════════════════════════════════════════════════════════════
PROGRAM Main
VAR
    // Timers
    tDoneHold       : TON;      // Holds irDoneFlag high so ESP32 can poll it
    tEStopDebounce  : TON;      // Prevents false E-stop triggers
    
    // Edge detection
    trig_NewCmd     : R_TRIG;   // Detects when ESP32 sends a new command
    trig_MoveDone   : R_TRIG;   // Detects when Sketch signals move complete
    trig_HomeDone   : R_TRIG;   // Detects when Sketch signals homing complete
    trig_SketchErr  : R_TRIG;   // Detects new error from Sketch
    
    // Working
    newCmdPending   : BOOL := FALSE;
END_VAR

// ════════════════════════════════════════════════════════════════════
// SECTION 1 — Mirror limit switches to Sketch (for homing sequence)
// ════════════════════════════════════════════════════════════════════
sk_LIM_X_MIN := LIM_X_MIN;
sk_LIM_Y_MIN := LIM_Y_MIN;
sk_LIM_D_MIN := LIM_D_MIN;

// ════════════════════════════════════════════════════════════════════
// SECTION 2 — Edge detection
// ════════════════════════════════════════════════════════════════════
// A new command is detected when hrCmd changes AND is not 0
trig_NewCmd(CLK  := (hrCmd <> PrevCmd) AND (hrCmd <> 0));
newCmdPending := trig_NewCmd.Q;

// Detect rising edges from Sketch completions
trig_MoveDone(CLK := sk_MoveComplete);
trig_HomeDone(CLK := sk_HomeComplete);
trig_SketchErr(CLK := sk_Error <> ERR_NONE);

// ════════════════════════════════════════════════════════════════════
// SECTION 3 — SAFETY: E-Stop and over-travel (highest priority)
// Evaluated every scan regardless of state
// ════════════════════════════════════════════════════════════════════

// Command E-Stop from ESP32
IF hrCmd = 5 THEN
    sk_EStop := TRUE;
    sk_ExecuteMove := FALSE;
    sk_HomeCmd := FALSE;
    MachineState := ST_ESTOPPED;
    irStatus := 4;
    PrevCmd := 5;
END_IF;

// Hard over-travel detection — only meaningful during motion
IF MachineState = ST_MOVING THEN
    IF LIM_X_MAX THEN
        sk_EStop := TRUE;
        sk_ExecuteMove := FALSE;
        irErrorCode := ERR_X_LIMIT;
        irStatus := 3;
        MachineState := ST_ERROR;
    END_IF;
    IF LIM_Y_MAX THEN
        sk_EStop := TRUE;
        sk_ExecuteMove := FALSE;
        irErrorCode := ERR_Y_LIMIT;
        irStatus := 3;
        MachineState := ST_ERROR;
    END_IF;
    IF LIM_D_MAX THEN
        sk_EStop := TRUE;
        sk_ExecuteMove := FALSE;
        irErrorCode := ERR_D_LIMIT;
        irStatus := 3;
        MachineState := ST_ERROR;
    END_IF;
END_IF;

// ════════════════════════════════════════════════════════════════════
// SECTION 4 — MAIN STATE MACHINE
// ════════════════════════════════════════════════════════════════════
CASE MachineState OF

    // ──────────────────────────────────────────────────────
    ST_IDLE: // 0 — Waiting for a command from ESP32
    // ──────────────────────────────────────────────────────
        sk_EStop := FALSE;
        sk_ExecuteMove := FALSE;
        sk_HomeCmd := FALSE;
        irStatus := 0;
        irDoneFlag := 0;
        irErrorCode := ERR_NONE;
        
        // Continuously update position feedback while idle
        irXPos := INT(sk_CurrentX);
        irYPos := INT(sk_CurrentY);
        irDPos := INT(sk_CurrentD);
        
        IF newCmdPending THEN
            
            CASE hrCmd OF
            
                // ──────────────────────────
                1: // MOVE ABSOLUTE
                //   X, Y, Delta all move to
                //   the exact step counts in
                //   the HR registers.
                // ──────────────────────────
                    sk_X_Target     := DINT(hrXTarget);
                    sk_Y_Target     := DINT(hrYTarget);
                    sk_D_Target     := DINT(hrDTarget);
                    sk_Speed        := UDINT(hrSpeed);
                    sk_Accel        := UDINT(hrAccel);
                    sk_MoveComplete := FALSE;
                    sk_ExecuteMove  := TRUE;
                    MachineState    := ST_MOVING;
                    irStatus        := 1;
                    
                // ──────────────────────────
                2: // MOVE RELATIVE
                //   Add HR values to current
                //   position. hrAxisMask
                //   controls which axes move.
                //   bit0=X, bit1=Y, bit2=Delta
                // ──────────────────────────
                    IF (hrAxisMask AND 16#01) <> 0 THEN
                        sk_X_Target := sk_CurrentX + DINT(hrXTarget);
                    ELSE
                        sk_X_Target := sk_CurrentX; // Hold position
                    END_IF;
                    
                    IF (hrAxisMask AND 16#02) <> 0 THEN
                        sk_Y_Target := sk_CurrentY + DINT(hrYTarget);
                    ELSE
                        sk_Y_Target := sk_CurrentY;
                    END_IF;
                    
                    IF (hrAxisMask AND 16#04) <> 0 THEN
                        sk_D_Target := sk_CurrentD + DINT(hrDTarget);
                    ELSE
                        sk_D_Target := sk_CurrentD;
                    END_IF;
                    
                    sk_Speed        := UDINT(hrSpeed);
                    sk_Accel        := UDINT(hrAccel);
                    sk_MoveComplete := FALSE;
                    sk_ExecuteMove  := TRUE;
                    MachineState    := ST_MOVING;
                    irStatus        := 1;
                    
                // ──────────────────────────
                3: // HOME ALL AXES
                //   Sketch drives each axis
                //   toward its MIN limit
                //   switch, zeros position,
                //   then moves to safe standby.
                // ──────────────────────────
                    sk_HomeComplete := FALSE;
                    sk_HomeCmd      := TRUE;
                    MachineState    := ST_HOMING;
                    irStatus        := 2;
                    
                // ──────────────────────────
                4: // DELTA ARM ONLY
                //   Move Delta arm to target
                //   while X and Y stay put.
                //   Used for pick sequence.
                // ──────────────────────────
                    sk_X_Target     := sk_CurrentX; // Hold X
                    sk_Y_Target     := sk_CurrentY; // Hold Y
                    sk_D_Target     := DINT(hrDTarget);
                    sk_Speed        := UDINT(hrSpeed);
                    sk_Accel        := UDINT(hrAccel);
                    sk_MoveComplete := FALSE;
                    sk_ExecuteMove  := TRUE;
                    MachineState    := ST_MOVING;
                    irStatus        := 1;
                    
            END_CASE;
            
            PrevCmd := hrCmd; // Record so we detect the NEXT new command
        END_IF;

    // ──────────────────────────────────────────────────────
    ST_MOVING: // 1 — Motors are running
    // ──────────────────────────────────────────────────────
        irStatus := 1;
        
        // Stream live position back to ESP32 every scan
        irXPos := INT(sk_CurrentX);
        irYPos := INT(sk_CurrentY);
        irDPos := INT(sk_CurrentD);
        
        // Check for normal completion from Sketch
        IF trig_MoveDone.Q THEN
            sk_ExecuteMove := FALSE;
            MachineState   := ST_WAIT_DONE;
        END_IF;
        
        // Check for Sketch-reported error
        IF trig_SketchErr.Q THEN
            sk_EStop    := TRUE;
            irErrorCode := sk_Error;
            MachineState := ST_ERROR;
        END_IF;

    // ──────────────────────────────────────────────────────
    ST_HOMING: // 2 — Homing sequence running in Sketch
    // ──────────────────────────────────────────────────────
        irStatus := 2;
        
        // Position reads during homing
        irXPos := INT(sk_CurrentX);
        irYPos := INT(sk_CurrentY);
        irDPos := INT(sk_CurrentD);
        
        IF trig_HomeDone.Q THEN
            sk_HomeCmd   := FALSE;
            MachineState := ST_WAIT_DONE;
        END_IF;
        
        IF trig_SketchErr.Q THEN
            sk_EStop    := TRUE;
            irErrorCode := sk_Error;
            MachineState := ST_ERROR;
        END_IF;

    // ──────────────────────────────────────────────────────
    ST_WAIT_DONE: // 5 — Pulse DONE flag so ESP32 can read it
    // ──────────────────────────────────────────────────────
        irDoneFlag  := 1;
        irStatus    := 0;   // Report idle; move is finished
        irErrorCode := ERR_NONE;
        
        // Update final position
        irXPos := INT(sk_CurrentX);
        irYPos := INT(sk_CurrentY);
        irDPos := INT(sk_CurrentD);
        
        // Hold the flag long enough for ESP32 polling to catch it
        tDoneHold(IN := TRUE, PT := T#300MS);
        IF tDoneHold.Q THEN
            tDoneHold(IN := FALSE);  // Reset timer for next use
            irDoneFlag   := 0;
            PrevCmd      := 0;       // Allow the same command to re-trigger
            hrCmd        := 0;       // Auto-clear command register
            sk_MoveComplete  := FALSE;
            sk_HomeComplete  := FALSE;
            MachineState := ST_IDLE;
        END_IF;

    // ──────────────────────────────────────────────────────
    ST_ESTOPPED: // 3 — Emergency stopped
    // ──────────────────────────────────────────────────────
        sk_EStop        := TRUE;
        sk_ExecuteMove  := FALSE;
        sk_HomeCmd      := FALSE;
        irStatus        := 4;
        
        // ESP32 must send CMD=9 (ClearFault) to exit E-Stop
        IF hrCmd = 9 THEN
            sk_EStop     := FALSE;
            irErrorCode  := ERR_NONE;
            PrevCmd      := 0;
            hrCmd        := 0;
            MachineState := ST_IDLE;
        END_IF;

    // ──────────────────────────────────────────────────────
    ST_ERROR: // 4 — Fault — requires explicit reset
    // ──────────────────────────────────────────────────────
        sk_EStop        := TRUE;
        sk_ExecuteMove  := FALSE;
        sk_HomeCmd      := FALSE;
        irStatus        := 3;
        // irErrorCode is preserved so ESP32 can read the fault cause
        
        // Clear with CMD=9
        IF hrCmd = 9 THEN
            sk_EStop     := FALSE;
            sk_Error     := ERR_NONE;
            irErrorCode  := ERR_NONE;
            PrevCmd      := 0;
            hrCmd        := 0;
            MachineState := ST_IDLE;
        END_IF;

END_CASE;

END_PROGRAM
```

---

## 6. Arduino Sketch Layer — `sketch.ino`

In Arduino PLC IDE, open the **Sketch** tab. This code runs `loop()` at full
STM32 speed alongside the PLC runtime. Add AccelStepper via **Library Manager**.

```cpp
// ════════════════════════════════════════════════════════════════════
// Sketch layer — Strawberry Harvester Step Pulse Generation
// Library required: AccelStepper (install via Library Manager)
// ════════════════════════════════════════════════════════════════════
#include <AccelStepper.h>

// ── Pin definitions — adjust to match your wiring ──────────────────
// If using an expansion module, replace with expansion output numbers
#define X_STEP_PIN   6    // EXP_O1 or direct GPIO
#define X_DIR_PIN    7    // EXP_O2
#define X_EN_PIN     8    // EXP_O3 (active LOW)

#define Y_STEP_PIN   9    // EXP_O4
#define Y_DIR_PIN    10   // EXP_O5
#define Y_EN_PIN     11   // EXP_O6

#define D_STEP_PIN   12   // EXP_O7
#define D_DIR_PIN    13   // EXP_O8
#define D_EN_PIN     14   // additional GPIO

// ── AccelStepper instances ─────────────────────────────────────────
// DRIVER mode: only needs STEP and DIR
AccelStepper stepperX(AccelStepper::DRIVER, X_STEP_PIN, X_DIR_PIN);
AccelStepper stepperY(AccelStepper::DRIVER, Y_STEP_PIN, Y_DIR_PIN);
AccelStepper stepperD(AccelStepper::DRIVER, D_STEP_PIN, D_DIR_PIN);

// ── Shared variables with PLC layer ───────────────────────────────
// These must be declared as 'extern' here and as external in PLC GVL.
// In Arduino PLC IDE, global PLC variables are accessible here directly
// if you declare them in a shared variable mapping. Match names exactly.
extern long     sk_X_Target;
extern long     sk_Y_Target;
extern long     sk_D_Target;
extern unsigned long sk_Speed;
extern unsigned long sk_Accel;
extern bool     sk_ExecuteMove;
extern bool     sk_MoveComplete;
extern bool     sk_HomeCmd;
extern bool     sk_HomeComplete;
extern bool     sk_EStop;
extern long     sk_CurrentX;
extern long     sk_CurrentY;
extern long     sk_CurrentD;
extern unsigned int sk_Error;

// Limit switch mirrors from PLC
extern bool     sk_LIM_X_MIN;
extern bool     sk_LIM_Y_MIN;
extern bool     sk_LIM_D_MIN;

// ── Internal sketch state ──────────────────────────────────────────
bool prevExecuteMove = false;
bool prevHomeCmd     = false;

enum HomingState {
    HOMING_IDLE,
    HOMING_X,       // Moving X toward min limit
    HOMING_X_BACK,  // Back off X limit by N steps
    HOMING_Y,       // Moving Y toward min limit
    HOMING_Y_BACK,
    HOMING_D,       // Moving Delta toward home limit
    HOMING_D_BACK,
    HOMING_DONE
};
HomingState homingPhase = HOMING_IDLE;

// ── Setup ──────────────────────────────────────────────────────────
void setup() {
    pinMode(X_EN_PIN, OUTPUT);
    pinMode(Y_EN_PIN, OUTPUT);
    pinMode(D_EN_PIN, OUTPUT);

    // Enable all drivers (active LOW)
    digitalWrite(X_EN_PIN, LOW);
    digitalWrite(Y_EN_PIN, LOW);
    digitalWrite(D_EN_PIN, LOW);

    // Safe starting defaults
    stepperX.setMaxSpeed(400);
    stepperX.setAcceleration(100);
    stepperY.setMaxSpeed(400);
    stepperY.setAcceleration(100);
    stepperD.setMaxSpeed(200);   // Delta arm: slower for gentle pick
    stepperD.setAcceleration(80);
    
    sk_Error = 0;
}

// ── Loop — runs continuously at full speed ─────────────────────────
void loop() {

    // ── E-STOP: hard stop everything, nothing else runs ───────────
    if (sk_EStop) {
        stepperX.stop();
        stepperY.stop();
        stepperD.stop();
        // Run one last cycle to apply deceleration-to-stop
        stepperX.run();
        stepperY.run();
        stepperD.run();
        prevExecuteMove = false;
        prevHomeCmd = false;
        homingPhase = HOMING_IDLE;
        // Update current position so PLC knows where we stopped
        sk_CurrentX = stepperX.currentPosition();
        sk_CurrentY = stepperY.currentPosition();
        sk_CurrentD = stepperD.currentPosition();
        return;
    }

    // ── EXECUTE MOVE — rising edge triggers new target load ────────
    if (sk_ExecuteMove && !prevExecuteMove) {
        // Load speed and acceleration
        float spd = (float)sk_Speed;
        float acc = (float)sk_Accel;

        stepperX.setMaxSpeed(spd);
        stepperX.setAcceleration(acc);
        stepperX.moveTo(sk_X_Target);

        stepperY.setMaxSpeed(spd);
        stepperY.setAcceleration(acc);
        stepperY.moveTo(sk_Y_Target);

        // Delta arm gets a calmer profile to protect the vacuum gripper
        stepperD.setMaxSpeed(spd * 0.6f);
        stepperD.setAcceleration(acc * 0.5f);
        stepperD.moveTo(sk_D_Target);

        sk_MoveComplete = false;
    }
    prevExecuteMove = sk_ExecuteMove;

    // ── RUN MOTORS during an active move ───────────────────────────
    if (sk_ExecuteMove) {
        stepperX.run();
        stepperY.run();
        stepperD.run();

        // All three axes must finish before signaling complete
        if (!stepperX.isRunning() &&
            !stepperY.isRunning() &&
            !stepperD.isRunning()) {
            sk_MoveComplete = true;
            // PLC state machine will clear sk_ExecuteMove
        }
    }

    // ── HOMING SEQUENCE ────────────────────────────────────────────
    if (sk_HomeCmd && !prevHomeCmd) {
        homingPhase = HOMING_X;
        stepperX.setMaxSpeed(150);   // Slow homing speed
        stepperX.setAcceleration(40);
        stepperX.move(-99999);       // Move toward X min limit
        sk_HomeComplete = false;
    }
    prevHomeCmd = sk_HomeCmd;

    if (sk_HomeCmd) {
        switch (homingPhase) {

            case HOMING_X:
                if (sk_LIM_X_MIN) {
                    // Hit X home limit — stop and zero
                    stepperX.stop();
                    stepperX.setCurrentPosition(0);
                    sk_CurrentX = 0;
                    homingPhase = HOMING_X_BACK;
                    stepperX.move(100); // Back off limit by 100 steps
                } else {
                    stepperX.run();
                }
                break;

            case HOMING_X_BACK:
                stepperX.run();
                if (!stepperX.isRunning()) {
                    stepperX.setCurrentPosition(0); // Re-zero after back-off
                    homingPhase = HOMING_Y;
                    stepperY.setMaxSpeed(150);
                    stepperY.setAcceleration(40);
                    stepperY.move(-99999);
                }
                break;

            case HOMING_Y:
                if (sk_LIM_Y_MIN) {
                    stepperY.stop();
                    stepperY.setCurrentPosition(0);
                    sk_CurrentY = 0;
                    homingPhase = HOMING_Y_BACK;
                    stepperY.move(100);
                } else {
                    stepperY.run();
                }
                break;

            case HOMING_Y_BACK:
                stepperY.run();
                if (!stepperY.isRunning()) {
                    stepperY.setCurrentPosition(0);
                    homingPhase = HOMING_D;
                    stepperD.setMaxSpeed(80);
                    stepperD.setAcceleration(30);
                    stepperD.move(-99999); // Retract delta arm to home
                }
                break;

            case HOMING_D:
                if (sk_LIM_D_MIN) {
                    stepperD.stop();
                    stepperD.setCurrentPosition(0);
                    sk_CurrentD = 0;
                    homingPhase = HOMING_D_BACK;
                    stepperD.move(50);
                } else {
                    stepperD.run();
                }
                break;

            case HOMING_D_BACK:
                stepperD.run();
                if (!stepperD.isRunning()) {
                    stepperD.setCurrentPosition(0);
                    homingPhase = HOMING_DONE;
                    sk_HomeComplete = true;
                }
                break;

            case HOMING_DONE:
                break; // PLC will clear sk_HomeCmd

            default:
                break;
        }
    }

    // ── ALWAYS update live position feedback ───────────────────────
    sk_CurrentX = stepperX.currentPosition();
    sk_CurrentY = stepperY.currentPosition();
    sk_CurrentD = stepperD.currentPosition();
}
```

---

## 7. ESP32 Side — Command Reference

Use any Modbus RTU library on the ESP32 (`ModbusMaster`, `ArduinoModbus`, etc.)
The RS485 DE/RE pin on the ESP32 should be driven HIGH to transmit, LOW to receive.

### Typical pick-sequence flow:
```
1. ESP32 homes all axes once at startup:
   Write HR[0]=3  → Opta homes X→Y→Delta
   Poll IR[0] until status=0 AND IR[5]=1 (done flag)

2. Vision system detects strawberry at row=2, col=4:
   Calculate step positions: X_steps = col * STEPS_PER_CELL
                             Y_steps = row * STEPS_PER_CELL

3. Move gantry to position (absolute move):
   Write HR[1]=X_steps, HR[2]=Y_steps, HR[3]=0  (keep delta up)
   Write HR[4]=800 (speed), HR[5]=200 (accel)
   Write HR[0]=1  (MoveAbs — triggers state machine)
   Poll until done flag

4. Lower delta arm to pick:
   Write HR[3]=PICK_DEPTH_STEPS
   Write HR[4]=300 (slower for gripper)
   Write HR[0]=4  (DeltaOnly)
   Poll until done flag
   → Vacuum fires here (separate relay / ESP32 GPIO)

5. Retract delta arm:
   Write HR[3]=0
   Write HR[0]=4
   Poll until done flag

6. Return to next position or drop zone: repeat from step 3
```

### Error recovery:
```
If IR[0] reads 3 (Error) or 4 (EStopped):
   Read IR[4] for error code
   Resolve physical fault (check limits, wiring)
   Write HR[0]=9  → Opta resets to IDLE
```

---

## 8. Key Configuration Checklist

- [ ] **Modbus RTU Slave** configured in PLC IDE Fieldbus panel: address 1, 115200 baud
- [ ] **HR 40001–40007** mapped to `hrCmd … hrAxisMask` GVL variables
- [ ] **IR 30001–30006** mapped to `irStatus … irDoneFlag` GVL variables
- [ ] **I1–I6** mapped to the six `LIM_*` GVL booleans
- [ ] **O1** mapped to master enable relay if needed
- [ ] **Shared variables** (`sk_*`) declared as External / mapped in PLC↔Sketch bridge
- [ ] **AccelStepper** installed in Library Manager
- [ ] **Step/Dir pins** adjusted in sketch to match your expansion module wiring
- [ ] **STEPS_PER_CELL** calculated: `(belt_pitch_mm / (full_steps × microstep_divisor))`
- [ ] **Limit switches wired NC** (normally closed) — safer; open = fault
- [ ] **Scan cycle** set to 10ms in Task Configuration
- [ ] **Test**: home first, then send small relative moves on each axis individually
```
