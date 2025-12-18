Hardware Integration — Developer Notes

- IMU (LSM9DS0 / LSM9DS1 / LSM6DSL + LIS3MDL)
  - Interface: I2C (preferred) or SPI. Ensure correct pull-ups on SDA/SCL lines if using I2C.
  - Addresses: verify sensor I2C addresses in `IMU.py` and `LSM*.py` before connecting multiple IMUs.
  - Testing: run the small test harness in `IMU.py` (or a short script) to verify readings and orientation.

- GPS
  - Interface: serial (UART) or USB-serial. When using Linux, `gpsd` is supported — configure `central_script.py`/`auto_navigation.py` to read from `/dev/ttyUSB0` or `gpsd` socket.
  - NMEA vs binary: confirm your GPS output format and parsing code.

- Moisture Sensors
  - Typical deployment: field nodes publish sensor payloads over MQTT (`moisture/data`). If using analog sensors, add an ADC (e.g., ADS1115) and publish readings from a microcontroller or Raspberry Pi.
  - Example payload (JSON):
    - `{"zone":"A","value":345,"unit":"raw","timestamp":"2025-12-18T12:00:00Z"}`

- Camera
  - Use V4L2-compatible cameras or USB webcams. `face_tracking.py` expects an OpenCV-compatible VideoCapture source (index or URL).
  - For streaming to web UI, use MJPEG or an HLS pipeline (mjpg-streamer, ffmpeg, or direct WebSocket frames).

- Motors & Motor Drivers
  - Motor control typically requires PWM and direction pins; use a motor driver (L298N, TB6612, or higher-power drivers). Map PWM channels in `central_script.py` or motor helper module.
  - If encoders are present, route encoder signals to interrupt-capable GPIO and integrate into navigation loops.

- Pump & Relay
  - Drive the pump via a properly rated relay or motor driver; avoid powering pump directly from low-voltage logic pins.
  - Add flyback diodes or snubbers if switching inductive loads.

- Power & Safety
  - Use common ground between logic and motor power. Keep high-current wiring separate and fused.
  - Add an emergency stop accessible from both the GUI (`E-Stop`) and a physical switch that cuts power to motor drivers/pump.

Software Integration — Developer Notes

- Running the stack
  - Recommended: create a virtual environment and install dependencies from `requirements.txt`.
    ```bash
    python -m venv .venv
    .venv\Scripts\activate   # Windows
    pip install -r requirements.txt
    ```

- Configuration
  - `central_script.py` contains MQTT and server settings; extract repeated values into a `config.py` or environment variables for easier deployment.

- MQTT topics & message formats (examples)
  - Sensor data: `moisture/data` — JSON `{zone, value, timestamp}`
  - IMU: `imu/data` — JSON `{ax,ay,az,gx,gy,gz,mag, timestamp}`
  - Robot status: `robot/status` — JSON `{mode,lat,lon,battery,errors}`
  - Control commands: `robot/control` — JSON `{cmd:"move",direction:"forward",speed:0.6}` or `{cmd:"pump",action:"on"}`

- Flask endpoints (examples)
  - `GET /status` — returns current robot status JSON
  - `POST /control` — accepts JSON control commands and forwards to MQTT/actuators
  - `GET /logs` — returns recent logs or CSV download links

- Extending the system
  - To add a new sensor: choose a topic name, publish JSON from the sensor node, then add a subscriber handler in `central_script.py` to process and log data.
  - To add a UI control: add a Flask endpoint (or socket event) and update `GUI.py` / `USER_INTERFACE` to call it; use existing MQTT topics when possible for decoupling.

- Logging & data files
  - Sensor readings are logged as CSV (see `moisture_data.csv` example). For production, consider rotating logs and using a lightweight DB (SQLite) for queries.

- Testing & debugging
  - Enable debug logging in `central_script.py` (`logging.DEBUG`) to trace message flows.
  - Use `mosquitto_pub` / `mosquitto_sub` to simulate sensors and commands during development.

Project File Tree (quick reference)

```
irrigationrobot/
├── central_script.py
├── auto_navigation.py
├── face_tracking.py
├── GUI.py
├── IMU.py
├── LSM3*/LSM*.py
├── README.md
├── DEVELOPER.md
├── requirements.txt
├── moisture_data.csv
├── USER_INTERFACE/
│   ├── GUI.py (alternate)
│   └── test.html
├── ESP32_ArduinoCode/
│   ├── ESPNOW_sender_PUMP/
│   └── test_irrV5/
└── Images/
    ├── controller.jpg
    └── ui_screenshot.jpg
```

Notes:
- Wrap hardware-specific code behind small interface modules (for example, an IMU wrapper and a motor controller wrapper) so it’s easy to mock in unit tests.
- Add the two user-facing images to `Images/` and reference them from `README.md` (I can patch the README to add image links if you want).
