# Autonomous Irrigation Robot
This project is a versatile, Wi-Fi-enabled agricultural robot designed for possible remote farming tasks. Its purpose is to mitigate tasks that could be automated. By combining robust hardware control with network-based communication, this mobile robot serves as a smart, scalable assistant for crop care and field maintenance.

At its core:
 - An ESP32 module handles real-time hardware execution, driving the motors and navigating the physical environment.
 - A Raspberry Pi that creates a web user interface to control the robot, collect data, and enable automated controls.

## Table of Contents
1. [New Additions](#1-new-additions)
2. [ESP32 Implementation](#2-esp32-implementation)  
	2.1 [Pin Location & Description](#21-pin-location--description)  
	2.2 [Curent Code & Test Code](#22-current-code--test-code)
3. [Raspberry Pi Implementation](#3-raspbery-pi-implementation)
4. [User Usage](#4-user-usage)  
	4.1 [Getting Started](#41-getting-started)  
	4.2 [Remote Control Overview](#42-remote-control-overview)  
	4.3 [Web Dashboard Overview](#43-web-dashboard-overview)  


## 1. New Additions
- Installed two new motors.
	- Implemented the new motor code to [`complete_v1`](./ESP32_ArduinoCode/complete_v1/complete_v1.ino)
	- Created test code
		- All motor movements, brakes, and speed tests
		- Remote control to motor connection

## 2. ESP32 Implementation
We utilize the ESP32 as the brain.  
- Recieving commands from either the remote control or the Web UI control.   
- Relay proper protocol for both the pump and the motors
### 2.1 Pin Location & Description
![esp32 current layout](/Images/IMG_3811.JPG)
### 2.2 Current Code & Test Code
The current code is [`complete_v1`](./ESP32_ArduinoCode/complete_v1/complete_v1.ino). It is able to recieve information from a remote control and web user interface. Then provide proper functionality to motor and pump.

For any necessary testing sometimes going to basics is the best.
- The [`simple_motor_test`](/ESP32_ArduinoCode/simple_motor_test/simple_motor_test.ino), only cares about testing the motor, checking if its able to go forward, turn, and in reverser, as well as break.
- The [`control_to_motor_test`](/ESP32_ArduinoCode/control_to_motor_test/control_to_motor_test.ino), we implement user control through a remote control. We test both motor and the pump, as well as the remote control receiver.


## 3. Raspbery Pi Implementation
The Pi acts as another form of controlling the robot through a web interface. In addition to regular movement and pump activation, it also provide visual. Visual like GPS location and Camera display. We can also automate the process of certain operations.

- The raspberry pi is able to recieve moisture value from a said zone and activate the pump at the location when given a threshold value.
- The Rasberry Pi is able to auto navigate given the proper coordinate
- Next step: remembering a path. Given that we have an IMU and GPD modules we should be able to save path for the robot to follow as a patrol route to the zones.

## 4. User Usage
### 4.1 Getting Started

1. **Power On:** Ensure the robot is has its two batteries fully charged and powered on. The system will boot and connect to the network (WiFi/Ethernet).
	- **TO-KNOW:** Since we install new motor they need a separate battery 
2. **Access the Controls:** We can access the robot full operations in two ways either by using the **Remote Control** provided or the **Web Interface**.
	- **Web Interface access:** Open a web browser and navigate to `http://<robot_ip>:5000` to access the web dashboard.

### 4.2 Remote Control Overview

![Controller Layout](Images/Control_Overview.png)

- **Armed/Mode Switch:** Select operating mode (disarmed, manual, auto-nav, face-track)
- **Movement Sticks:** Left stick for forward/backward; right stick for left/right turns
- **Speed Control:** The motor has three gears this switch can travel to each of them
- **Pump Control:** Dedicated pump on/off switch for manual irrigation
- **Power Button:** Turn the robot on/off

### 4.3 Web Dashboard Overview

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

- **Moisture & Irrigation:**
  - **Zone Moisture Value:** Show current soil moisture readings
  - **Threshold slider:** Set the soil moisture level at which to trigger automatic watering
  - **Pump controls:** Manual On/Off, or Auto (triggers when moisture drops below threshold)

- **Location Tracking (Map):**
  - Red pin shows robot's current GPS position
  - Click on the map to set waypoints for auto-navigation
  - Planned route is displayed before execution

- **Camera Display:**
  - Show what the robot sees in front it.
