# Autonomous Irrigation Robot

An intelligent autonomous robot system designed for precision agriculture and irrigation management. This project combines computer vision, GPS navigation, IMU sensors, and IoT connectivity to create a comprehensive agricultural automation solution.

## 🚀 Features

### Core Capabilities
- **Autonomous Navigation**: GPS-based waypoint navigation with Extended Kalman Filter (EKF) for precise positioning
- **Face Tracking**: Computer vision-based human detection and following using PID control
- **Smart Irrigation**: Automated moisture-based irrigation control with zone management
- **Real-time Monitoring**: Live camera feed and sensor data visualization
- **Remote Control**: Web-based dashboard for manual operation and monitoring
- **Multi-mode Operation**: Basic movement, auto-navigation, and face tracking modes

### Hardware Integration
- **IMU Support**: Compatible with BerryIMU v1, v2, and v3 (LSM9DS0, LSM9DS1, LSM6DSL/LIS3MDL)
- **GPS Integration**: Real-time positioning using GPSD
- **MQTT Communication**: Wireless sensor network connectivity
- **Camera System**: Live video streaming and computer vision processing
- **Pump Control**: Automated irrigation pump management

## 🏗️ System Architecture

```
├── central_script.py      # Main control system and Flask web server
├── face_tracking.py       # Computer vision and PID control for face tracking
├── auto_navigation.py     # GPS navigation and path planning
├── IMU.py                # IMU sensor interface and data processing
├── GUI.py                # Web interface HTML/CSS/JavaScript
├── LSM*.py               # IMU sensor register definitions
├── USER_INTERFACE/       # Alternative web interface
└── GUI_TEST/            # Testing interface
```

## 📋 Requirements

### Hardware Requirements
- Raspberry Pi (3B+ or newer recommended)
- BerryIMU (v1, v2, or v3)
- GPS module compatible with GPSD
- Camera module
- Irrigation pump system
- Moisture sensors with MQTT capability
- Robot chassis with motor control

### Software Requirements
- Python 3.7+
- Raspberry Pi OS or compatible Linux distribution
- All dependencies listed in `requirements.txt`

## 🔧 Installation

1. **Clone the repository:**
   ```bash
   git clone https://github.com/greatroboticslab/irrigationrobot.git
   cd irrigationrobot
   ```

2. **Install Python dependencies:**
   ```bash
   pip install -r requirements.txt
   ```

3. **Enable I2C and configure GPS:**
   ```bash
   sudo raspi-config
   # Enable I2C in Interface Options
   # Install and configure GPSD
   sudo apt-get install gpsd gpsd-clients
   ```

4. **Configure MQTT settings:**
   Edit `central_script.py` and update the MQTT server configuration:
   ```python
   MQTT_SERVER = "your_mqtt_broker_ip"
   MQTT_PORT = 1883
   ```

## 🚀 Usage

### Starting the System

1. **Run the main application:**
   ```bash
   python central_script.py
   ```

2. **Access the web interface:**
   Open your browser and navigate to `http://[robot_ip]:5000`

### Operation Modes

#### 1. Basic Movement Mode
- Manual control via web interface
- Direct robot movement commands
- Manual pump control
- Real-time sensor monitoring

#### 2. Auto-Navigation Mode
- GPS waypoint navigation
- Path planning and following
- Obstacle avoidance (if sensors configured)
- Automatic irrigation along route

#### 3. Face Tracking Mode
- Computer vision-based human detection
- PID-controlled following behavior
- Adjustable tracking parameters
- Safety features and emergency stop

### Web Interface Features

- **Live Camera Feed**: Real-time video streaming from robot camera
- **GPS Mapping**: Interactive map showing robot position and planned routes
- **Sensor Dashboard**: Moisture levels, IMU data, and system status
- **Control Panel**: Movement controls, mode selection, and emergency stops
- **Configuration**: PID tuning, irrigation thresholds, and system settings

## 📊 Sensor Integration

### Moisture Sensors
The system supports multiple moisture sensor zones with individual thresholds:
- Zone A, B, C configuration
- MAC address-based sensor identification
- Automatic irrigation triggering
- Data logging to CSV files

### IMU Integration
Supports multiple IMU versions:
- **BerryIMU v1**: LSM9DS0 (accelerometer, gyroscope, magnetometer)
- **BerryIMU v2**: LSM9DS1 (accelerometer, gyroscope, magnetometer)
- **BerryIMU v3**: LSM6DSL + LIS3MDL (accelerometer, gyroscope, magnetometer)

### GPS Navigation
- Real-time positioning using GPSD
- UTM coordinate conversion for local navigation
- Extended Kalman Filter for position estimation
- Waypoint-based path planning

## 🔧 Configuration

### MQTT Topics
- `robot/control` - Robot movement commands
- `robot/rail` - Rail/drip line control
- `robot/detections` - Computer vision detections
- `robot/camera` - Camera feed data
- `robot/pump` - Pump control commands
- `moisture/data` - Sensor data from field devices
- `imu/data` - IMU sensor readings

### PID Controller Tuning
Face tracking PID parameters can be adjusted via the web interface:
- **Kp**: Proportional gain (default: 0.5)
- **Ki**: Integral gain (default: 0.0001)
- **Kd**: Derivative gain (default: 0.25)

### Irrigation Thresholds
- Configurable moisture thresholds per zone
- Automatic/manual pump control modes
- Zone-based MAC address mapping

## 🛠️ Development

### File Structure
- `central_script.py`: Main application with Flask server and MQTT handling
- `face_tracking.py`: Computer vision processing and PID control
- `auto_navigation.py`: GPS navigation and path planning algorithms
- `IMU.py`: Hardware abstraction layer for IMU sensors
- `GUI.py`: Web interface HTML/CSS/JavaScript generation
- `LSM*.py`: Register definitions for IMU sensor variants

### Adding New Features
1. Sensor integration: Add new MQTT topics and handlers in `central_script.py`
2. Navigation algorithms: Extend `auto_navigation.py` with new path planning methods
3. Computer vision: Enhance detection algorithms in `face_tracking.py`
4. Web interface: Modify `GUI.py` for new dashboard features

## 🔒 Safety Features

- **Emergency Stop**: Immediate robot shutdown via web interface or MQTT
- **Sensor Monitoring**: Continuous health checks for all sensors
- **Fail-safe Modes**: Automatic stop on sensor failures
- **Manual Override**: Always available manual control mode

## 📈 Data Logging

The system automatically logs:
- Moisture sensor readings with timestamps
- GPS coordinates and heading data
- System events and errors
- Irrigation events and durations

Data is stored in CSV format for analysis and monitoring.

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/new-feature`)
3. Commit your changes (`git commit -am 'Add new feature'`)
4. Push to the branch (`git push origin feature/new-feature`)
5. Create a Pull Request

## 📄 License

This project is part of the Great Robotics Lab research initiative. Please refer to the repository license for usage terms.

## 🆘 Troubleshooting

### Common Issues

1. **IMU Not Detected**
   - Check I2C connections and enable I2C in raspi-config
   - Verify IMU power supply and wiring

2. **GPS Not Working**
   - Ensure GPSD is installed and running
   - Check GPS module connections and antenna

3. **MQTT Connection Failed**
   - Verify MQTT broker IP address and port
   - Check network connectivity

4. **Camera Feed Not Loading**
   - Ensure camera module is enabled and connected
   - Check camera permissions and drivers

### Debug Mode
Enable debug logging by modifying the logging level in `central_script.py`:
```python
logging.basicConfig(level=logging.DEBUG)
```

## 📞 Support

For technical support and questions:
- Create an issue in the GitHub repository
- Contact the Great Robotics Lab team
- Check the project wiki for additional documentation

---

**Great Robotics Lab** - Advancing Agricultural Automation Through Robotics
