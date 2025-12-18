# Autonomous Irrigation Robot
This is an autonomous irrigation robot for precision watering, monitoring, and basic field assistance. This file contains the information to inform both the user and the developers.

## Features
- **Real-time Monitoring:** Live camera feed and sensor data visualization
- **Smart Irrigation:** Moisture-based irrigation control with zone management

### Operation Modes
- **Manual Control:** Web dashboard or remote controller for manual movement and pump control
- **Auto-Navigation:** GPS/IMU-based waypoint navigation with path following
- **Face Tracking:** Computer-vision-based human detection and PID following

---

## Hardware and Software Integration (Developer)

### Hardware Integration — Key Points

- IMU (LSM9DS0 / LSM9DS1 / LSM6DSL + LIS3MDL)
	- Interface: I2C (preferred) or SPI. Verify addresses in `IMU.py` / `LSM*.py`.
	- Test: run the IMU test harness to confirm orientation and sensor health.

- GPS
	- Interface: UART/USB-serial or `gpsd` on Linux. Configure `auto_navigation.py` for the correct device/socket.

- Moisture Sensors
	- Usually published via MQTT topic `moisture/data` from field nodes. Use ADC (ADS1115) for analog sensors.
	- Example payload: `{"zone":"A","value":345,"unit":"raw","timestamp":"2025-12-18T12:00:00Z"}`

- Camera
	- OpenCV-compatible (VideoCapture). For web streaming, use MJPEG/HLS or an ffmpeg pipeline.

- Motors & Drivers
	- Use motor drivers (TB6612, L298N, or similar) and isolate motor power with fuses; map PWM in `central_script.py`.

- Pump & Relay
	- Drive via rated relay/driver and include flyback protection for inductive loads.

- Power & Safety
	- Use common ground, separate high-current wiring, add fuses, and provide an accessible physical E-Stop.

### Software Integration — Key Points

- Development environment
	- Create a Python venv and install `requirements.txt`.
		```bash
		python -m venv .venv
		.venv\Scripts\activate   # Windows
		pip install -r requirements.txt
		```

- Configuration
	- `central_script.py` holds server and MQTT settings. Consider moving to `config.py` or environment variables.

- MQTT topics (examples)
	- `moisture/data` — `{zone, value, timestamp}`
	- `imu/data` — `{ax,ay,az,gx,gy,gz,mag,timestamp}`
	- `robot/status` — `{mode,lat,lon,battery,errors}`
	- `robot/control` — `{cmd: "move", direction: "forward", speed: 0.6}` or `{cmd: "pump", action: "on"}`

- HTTP endpoints (examples in `central_script.py`)
	- `GET /status` — current status
	- `POST /control` — send command JSON
	- `GET /logs` — download CSV logs

- Logging
	- Sensor data is logged to CSV (see `moisture_data.csv`). For production, consider log rotation or SQLite.

### Project File Tree (quick reference)

```
irrigationrobot/
├── central_script.py
├── auto_navigation.py
├── face_tracking.py
├── GUI.py
├── IMU.py
├── LSM*.py
├── README.md
├── DEVELOPER.md
├── requirements.txt
├── moisture_data.csv
├── USER_INTERFACE/
│   ├── GUI.py
│   └── test.html
├── ESP32_ArduinoCode/
│   ├── ESPNOW_sender_PUMP/
│   └── test_irrV5/
└── Images/
		├── controller.jpg
		└── ui_screenshot.jpg
```

---

## Users: Operating the Robot

### Getting Started

1. **Power On:** Ensure the robot is fully charged and powered on. The system will boot and connect to the network (WiFi/Ethernet).
2. **Access the Interface:** Open a web browser and navigate to `http://<robot_ip>:5000` to access the web dashboard, or use the physical remote controller (if available).
3. **Check Status:** The dashboard shows:
   - **Robot Status:** Online/Offline, current battery level, and active mode
   - **Location:** GPS coordinates and real-time position on an interactive map
   - **Sensors:** Current moisture levels in each zone, IMU data (tilt, orientation), and any warnings

### Control Overview

#### Remote Controller (Physical)

![Controller Layout](Images/Control_Overview.png)

- **Armed/Mode Switch:** Select operating mode (disarmed, manual, auto-nav, face-track)
- **Movement Sticks:** Left stick for forward/backward; right stick for left/right turns
- **Pump Control:** Dedicated pump on/off switch for manual irrigation
- **Power Button:** Turn the robot on/off

#### Web Dashboard (GUI)

![Web Dashboard](Images/GUI_control.png)

The web interface provides:

- **Mode Selection:** Choose between:
  - **Basic Movement:** Direct control via arrow buttons or game controller
  - **Auto-Navigation:** Set GPS waypoints on the map; robot autonomously drives to them
  - **Face Tracking:** Robot follows a detected person using the camera

- **Movement Controls:** Arrow buttons or analog stick (if using a gamepad) for manual movement
  - Forward / Backward / Left / Right
  - Speed selector (Low / Medium / High)
  - **STOP** button for immediate halt

- **Gear Speed:** Adjust robot speed (Low for precise work, High for faster coverage)

- **PID Tuning (Face Tracking Mode):**
  - **Kp, Ki, Kd sliders:** Fine-tune how aggressively the robot follows a face
  - Higher Kp = faster response; adjust if the robot oscillates or over-shoots

- **Moisture & Irrigation:**
  - **Zone A, B, C displays:** Show current soil moisture readings
  - **Threshold slider:** Set the soil moisture level at which to trigger automatic watering
  - **Pump controls:** Manual On/Off, or Auto (triggers when moisture drops below threshold)
  - **Drip Line controls:** Forward/Backward/Stop to reposition the watering line

- **Location Tracking (Map):**
  - Red pin shows robot's current GPS position
  - Click on the map to set waypoints for auto-navigation
  - Planned route is displayed before execution

- **Emergency Controls:**
  - **E-Stop (red button):** Immediately cuts power to motors and pump — use in emergencies
  - **Resume (green button):** Re-enable motors after E-Stop

### Operation Workflow

#### Manual Operation (Basic Movement)
1. Select **Basic Movement** mode
2. Use arrow buttons or physical remote to drive
3. Use **Pump On** to start watering; **Pump Off** to stop
4. Monitor the live camera feed to see where the robot is heading

#### Automated Irrigation (Auto-Navigation + Moisture Control)
1. Select **Auto-Navigation** mode
2. On the map, click to add waypoints (the robot will visit them in sequence)
3. Set irrigation **Threshold** (e.g., 30% soil moisture) in the **Moisture** panel
4. Set pump to **Auto** — the robot will water zones when moisture falls below threshold
5. Press **Start** to begin the route; monitor progress on the map
6. Robot will stop at each waypoint and check moisture levels

#### Human Following (Face Tracking)
1. Select **Face Tracking** mode
2. Robot activates the camera and searches for a person's face
3. Once detected (green box on camera feed), the robot automatically follows
4. Adjust **PID parameters** if following is too slow or jerky
5. Press **STOP** to halt; E-Stop for emergency cutoff

### Safety Tips

- **Always keep an eye on the robot** — supervise during initial operations
- **Avoid obstacles:** The robot has limited obstacle detection; clear the path
- **Check battery:** Monitor battery level on the dashboard; return robot to charge when below 20%
- **Water responsibly:** Manual pump operation can waste water; use Auto mode for efficiency
- **Emergency Stop:** The red **E-Stop** button is always available — use it if anything goes wrong
- **Keep dry:** While the robot is water-resistant, avoid submersion; dry wet areas before extended storage

### Troubleshooting (Users)

| Issue | Solution |
|-------|----------|
| Robot not responding | Check power, WiFi connection, and reload the web page |
| GPS signal lost | Move to open area away from buildings; check antenna connection |
| Pump not spraying | Check water line is connected, not kinked; verify pump is not on E-Stop |
| Slow movement | Reduce load, check battery level, or clean wheels |
| Camera feed frozen | Refresh browser or restart the robot |

---
