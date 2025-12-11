# ---------------------------------------------------
# -------------------- Main File --------------------

# -------------------- Libraries --------------------
import os
import cv2
import csv
import sys
import time
import json
import math
import gpsd
import base64
import serial
import logging
import threading
import numpy as np
import paho.mqtt.client as mqtt
from datetime import datetime
from multiprocessing import Process, Queue, Event
from flask import Flask, Response, jsonify, request, render_template_string, render_template

# ---------------- Internal Modules -----------------
import IMU
import GUI

from face_tracking import face_tracking_process
from auto_navigation import auto_navigation_process

# ---------------------------------------------------
# --------------- MQTT Configuration ----------------
MQTT_SERVER = "192.168.1.104"  # Update if different
MQTT_PORT   = 1883

# -------------------- Channels ---------------------
MQTT_TOPIC_COMMAND      = "robot/control"
MQTT_RAIL_TOPIC_COMMAND = "robot/rail"
MQTT_TOPIC_DETECTIONS   = "robot/detections"
MQTT_TOPIC_CAMERA       = "robot/camera"
MQTT_TOPIC_PUMP         = "robot/pump"

MQTT_TOPIC_IMU          = "imu/data"
MQTT_TOPIC_DATA         = "moisture/data"
# ---------------------------------------------------
# ---------------------------------------------------

# -------------------- Constants --------------------
# PID CONTROLLER PARAMETES
FRAME_WIDTH         = 640
FRAME_HEIGHT        = 480
FRAME_CENTER_X      = FRAME_WIDTH // 2
# CONFIGURATION FLAGS
ENABLE_FRAME_FLIP   = True  # Set to False to disable frame flipping
INVERT_YAW_CONTROL  = False  # Set to True if robot moves opposite to desired direction
# MODE CONFIGURATIONS
MANUAL_MODE  = 0
AUTO_MODE   = 1

# --------------------- Globals ---------------------
latest_moist_value  = None
latest_detection    = None
latest_camera_frame = None
output_frame        = None
current_lat         = None
current_lon         = None
pump_cmd			= "0"
set_threshold       = 10
robot_heading       = 0.0
gps_data            = []
e_stop_active       = False  # E-Stop state

# MOVEMENTS MODE CONTROLS - DEFAULT
current_movement_mode   = 'basic_movement'
# PUMP MODE - DEDAULT
current_pump_mode       = MANUAL_MODE

# LOCKS
lock                = threading.Lock()
gps_data_lock       = threading.Lock()

# QUEUES for inter-process communication
command_queue       = Queue()
detection_queue     = Queue()
camera_frame_queue  = Queue()
gps_data_queue      = Queue()
imu_queue           = Queue()

# EVENT to control processes
stop_event          = Event()

# Flask App
app = Flask(__name__)


# CSV section
filename = 'moisture_data.csv'
file_exists = os.path.isfile(filename)

csv_file = open(filename, mode='a', newline='')
writer = csv.writer(csv_file)
if not file_exists:
    writer.writerow(['Timestamp', 'Mac Address', 'Data'])
    

# Mainly for the Remote Pump Controls
zone_A_macs = {"C0:49:EF:65:3A:24", "BB:BB:BB:BB:BB:BB"} # Change these into different mac addresses or add in the new mac
zone_B_macs = {"AA:AA:AA:AA:AA:AA", "BB:BB:BB:BB:BB:BB"} 
zone_C_macs = {"AA:AA:AA:AA:AA:AA", "BB:BB:BB:BB:BB:BB"} # Not using Zone C for demo 6/16/2025

# Independent Thresholds for moisture
zone_threshold = {
    "A": set_threshold,
    "B": set_threshold,
    "C": set_threshold 
}
zone_macs = {
    "A": zone_A_macs,
    "B": zone_B_macs,
    "C": zone_C_macs 
}

pump_states = {
    "A": False,
    "B": False,
    "C": False
}


# MQTT setup and functions
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"Connected to MQTT server {MQTT_SERVER} successfully.")
        client.subscribe([(MQTT_TOPIC_DETECTIONS, 0), 
                          (MQTT_TOPIC_CAMERA, 0),
                          (MQTT_TOPIC_PUMP, 0), 
                          (MQTT_TOPIC_DATA, 0)])
    else:
        print(f"Failed to connect to MQTT server, return code {rc}")

def on_message(client, userdata, msg):
    try:
        if msg.topic == MQTT_TOPIC_DETECTIONS:
            detection_data = json.loads(msg.payload.decode())
            detection_queue.put(detection_data)
        elif msg.topic == MQTT_TOPIC_CAMERA:
            camera_data = json.loads(msg.payload.decode())
            image_b64 = camera_data.get('image', '')
            if image_b64:
                image_bytes = base64.b64decode(image_b64)
                np_arr = np.frombuffer(image_bytes, np.uint8)
                image = cv2.imdecode(np_arr, cv2.IMREAD_COLOR)
                if image is not None:
                    camera_frame_queue.put(image)
                else:
                    print("Failed to decode camera image.")
            else:
                print("No image data found in camera message.")
        elif msg.topic == MQTT_TOPIC_DATA:
                    global latest_moist_value
                    global set_threshold
                    global pump_cmd
                    sensor_data = json.loads(msg.payload.decode())
                    mac = sensor_data.get("mac")
                    value = sensor_data.get("value")
                
                    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                    writer.writerow([timestamp, mac, value])
                    csv_file.flush()

                    latest_moist_value = value
                    print(f"Saved data: {timestamp}, {mac}, {value}")
                    # Make a list of zones for the mac addresses
                    # If value in zone A, B, etc is low
                        # Set Zone pump on "zone cmd"
                    # else
                        # Set zones pump off "zone cmd"
                        
                    zone = None
                    for z, macs in zone_macs.items():
                        if mac in macs:
                            zone = z
                            break
                    
                    # Do actions based on findings and on activation of auto
                    if current_pump_mode == AUTO_MODE:
                        if zone:
                            threshold = zone_threshold[zone]
                            if value < set_threshold and pump_cmd == "0":
                                pump_cmd = "1"
                                print(f"[ZONE {zone}] Moisture below threshold. Turning Pump ON")
                                client.publish(MQTT_TOPIC_PUMP, pump_cmd)
                                pump_states[zone] = True
                            elif value >= set_threshold and pump_cmd == "1":
                                pump_cmd = "0"
                                print(f"[ZONE {zone}] Moisture good. Turning Pump OFF")
                                client.publish(MQTT_TOPIC_PUMP, pump_cmd)
                                pump_states[zone] = False
                            
                        else:
                            print(f"Unknown MAC address {mac}. Ignoring.")

            
    except Exception as e:
        print(f"Error handling message on topic {msg.topic}: {e}")


# Initialize MQTT Client
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect(MQTT_SERVER, MQTT_PORT, 60)
client.loop_start()

# Initialize IMU
IMU.detectIMU()
if IMU.BerryIMUversion == 99:
    print("No BerryIMU found... exiting")
    sys.exit()
IMU.initIMU()

# Connect to GPSD
gpsd.connect()

def calculate_heading():
    """
    Calculate the robot's heading using IMU readings.
    Returns the heading in degrees.
    """
    ACCx = IMU.readACCx()
    ACCy = IMU.readACCy()
    ACCz = IMU.readACCz()
    MAGx = IMU.readMAGx()
    MAGy = IMU.readMAGy()
    MAGz = IMU.readMAGz()

    # Normalize accelerometer raw values.
    acc_magnitude = math.sqrt(ACCx ** 2 + ACCy ** 2 + ACCz ** 2)
    if acc_magnitude == 0:
        print("Error: Accelerometer magnitude is zero.")
        return 0
    accXnorm = ACCx / acc_magnitude
    accYnorm = ACCy / acc_magnitude

    pitch = math.asin(accXnorm)
    if math.cos(pitch) == 0:
        roll = 0
    else:
        roll = -math.asin(accYnorm / math.cos(pitch))

    # Tilt compensation
    magXcomp = MAGx * math.cos(pitch) + MAGz * math.sin(pitch)
    magYcomp = (MAGx * math.sin(roll) * math.sin(pitch) +
               MAGy * math.cos(roll) -
               MAGz * math.sin(roll) * math.cos(pitch))

    heading = math.degrees(math.atan2(magYcomp, magXcomp))
    if heading < 0:
        heading += 360

    return heading

def receive_gps_data():
    global current_lat, current_lon
    try:
        packet = gpsd.get_current()
        if packet.mode >= 2:
            current_lat = packet.lat
            current_lon = packet.lon
            return current_lat, current_lon
        else:
            return None, None
    except Exception as e:
        print(f"Error retrieving GPS data: {e}")
        return None, None

def main_loop():
        global latest_detection, latest_camera_frame
        global output_frame, lock, e_stop_active
        global current_lat, current_lon, robot_heading, gps_data
        global current_movement_mode
        check = False

        # Initialize last_img with a black image
        last_img = np.zeros((FRAME_HEIGHT, FRAME_WIDTH, 3), dtype=np.uint8)

        try:
            while True:
                # Update last_img if a new frame is available
                while not camera_frame_queue.empty():
                    last_img = camera_frame_queue.get()

                # Use the last available frame
                img = last_img.copy()

                # Optionally flip the frame
                if ENABLE_FRAME_FLIP:
                    img = cv2.flip(img, 1)  # Flip the image horizontally

                # ... (additional processing like drawing text, handling modes, etc.)

                # Acquire lock to set the global output frame
                with lock:
                    output_frame = img.copy()

                # Small sleep to reduce CPU usage
                time.sleep(0.05)


                # Display center offset on the frame
                cv2.putText(img, f"Mode: {current_movement_mode}", (10, 30),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

                # Optionally display IMU heading
                imu_heading = calculate_heading()
                cv2.putText(img, f"IMU Heading: {imu_heading:.2f}", (10, 60),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

                # Optionally display E-Stop status
                if e_stop_active:
                    cv2.putText(img, "E-STOP ACTIVE!", (10, 90),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)

                # Update GPS data
                current_lat, current_lon = receive_gps_data()
                if current_lat is not None and current_lon is not None:
                    with gps_data_lock:
                        gps_data.append({
                            'GPS_Lat': current_lat,
                            'GPS_Lon': current_lon,
                            'Heading': imu_heading
                        })

                # Check for e-stop activation
                if e_stop_active:
                    print("E-Stop is active. Stopping the robot.")
                    front_back_command = 64  # Stop
                    side_side_command = 64   # Neutral steering
                    command_string = f"{front_back_command} {side_side_command}"
                    client.publish(MQTT_TOPIC_COMMAND, command_string)
                    with lock:
                        output_frame = img.copy()
                    time.sleep(0.1)
                    continue  # Skip the rest of the loop

                # Handle different modes
                if current_movement_mode == 'face_tracking':
                    # Send data to face tracking process
                    if not detection_queue.empty():
                        detection = detection_queue.get()
                        command_queue.put(('face_tracking', detection))
                    else:
                        # No detection received; stop the robot
                        front_back_command = 64  # Stop
                        side_side_command = 64   # Neutral steering
                        command_string = f"{front_back_command} {side_side_command}"
                        print("Stopping (no detection received)")
                        client.publish(MQTT_TOPIC_COMMAND, command_string)
                elif current_movement_mode == 'auto_navigation':
                    # Auto-Navigation Mode
                    check = False
                    pass  # Handled by auto_navigation_process
                elif current_movement_mode == 'basic_movement':
                    # Basic Movement Mode
                    if not check:
                        print("Basic Movement mode is active.")
                        check = True
                    # No automatic commands; manual movement commands are handled via Flask routes
                else:
                    # Unknown mode; stop the robot for safety
                    front_back_command = 64  # Stop
                    side_side_command = 64   # Neutral steering
                    command_string = f"{front_back_command} {side_side_command}"
                    print("Unknown mode. Stopping the robot.")
                    client.publish(MQTT_TOPIC_COMMAND, command_string)
                    check = False

                # Acquire lock to set the global output frame
                with lock:
                    output_frame = img.copy()

                # Small sleep to reduce CPU usage
                time.sleep(0.05)

        except Exception as e:
            print(f"An error occurred: {e}")

        finally:
            # Cleanup
            front_back_command = 64  # Stop
            side_side_command = 64   # Neutral steering
            command_string = f"{front_back_command} {side_side_command}"
            client.publish(MQTT_TOPIC_COMMAND, command_string)
            client.loop_stop()
            client.disconnect()

def generate():
    global output_frame, lock
    # Continuously generate frames
    while True:
        with lock:
            if output_frame is None:
                continue
            # Encode the frame in JPEG format
            (flag, encoded_image) = cv2.imencode(".jpg", output_frame)
            if not flag:
                continue
        # Yield the output frame in byte format
        yield(b'--frame\r\n' b'Content-Type: image/jpeg\r\n\r\n' +
              bytearray(encoded_image) + b'\r\n')
        time.sleep(0.05)  # Adjust to control frame rate


  # (Same as in your original script)

html_content = GUI.get_html()
# Define Flask route functions here
@app.route("/")
def index():
    return render_template_string(html_content)

@app.route("/video_feed")
def video_feed():
    return Response(generate(),
                    mimetype="multipart/x-mixed-replace; boundary=frame")

@app.route("/estop", methods=['POST'])
def estop():
    global e_stop_active
    e_stop_active = True
    print("E-Stop activated!")
    front_back_command = 64  # Stop
    side_side_command = 64   # Neutral steering
    command_string = f"{front_back_command} {side_side_command}"
    client.publish(MQTT_TOPIC_COMMAND, command_string)
    return jsonify({"status": "E-Stop activated"})

@app.route("/undo_estop", methods=['POST'])
def undo_estop():
    global e_stop_active
    e_stop_active = False
    print("E-Stop deactivated!")
    return jsonify({"status": "E-Stop deactivated"})

@app.route("/increase_face_area", methods=['POST'])
def increase_face_area():
    # Handle in face_tracking.py
    command_queue.put(('increase_face_area', None))
    return jsonify({"status": "Face area increased"})

@app.route("/decrease_face_area", methods=['POST'])
def decrease_face_area():
    # Handle in face_tracking.py
    command_queue.put(('decrease_face_area', None))
    return jsonify({"status": "Face area decreased"})

@app.route("/move_center_left", methods=['POST'])
def move_center_left():
    # Handle in face_tracking.py
    command_queue.put(('move_center_left', None))
    return jsonify({"status": "Center moved left"})

@app.route("/move_center_right", methods=['POST'])
def move_center_right():
    # Handle in face_tracking.py
    command_queue.put(('move_center_right', None))
    return jsonify({"status": "Center moved right"})

@app.route('/get_gps_data', methods=['GET'])
def get_gps_data_route():
    with gps_data_lock:
        if gps_data:
            sanitized_gps_data = [{
                'GPS_Lat': float(item.get('GPS_Lat', 0)),
                'GPS_Lon': float(item.get('GPS_Lon', 0)),
                'Heading': float(item.get('Heading', 0)),
                'Estimated_Lat': float(item.get('Estimated_Lat', 0)),
                'Estimated_Lon': float(item.get('Estimated_Lon', 0)),
                'Estimated_Theta': float(item.get('Estimated_Theta', 0))
            } for item in gps_data]
        else:
            sanitized_gps_data = []
    return jsonify(sanitized_gps_data)

@app.route('/initial_gps', methods=['GET'])
def initial_gps():
    lat, lon = receive_gps_data()
    if lat is not None and lon is not None:
        return jsonify({"lat": lat, "lon": lon})
    else:
        print("Initial GPS data unavailable.")
        return jsonify({"lat": 0.0, "lon": 0.0})
    
@app.route('/get_moistValue')
def get_moistValue():
    if latest_moist_value is None:
        display_value = "No data received yet"
    else:
        display_value = latest_moist_value
    return jsonify({"moisture_value": display_value})

@app.route('/update_MoistThreshold', methods=['POST'])
def update_MoistThreshold():
    global set_threshold
    set_threshold = request.get_json()
    return jsonify({"Moisture senssor's threshold value updated to": set_threshold})

@app.route("/set_mode", methods=['POST'])
def set_mode():
    global current_movement_mode, stop_event
    data = request.get_json()
    mode = data.get('mode', 'basic_movement')
    if mode in ['basic_movement', 'auto_navigation', 'face_tracking']:
        current_movement_mode = mode
        print(f"Mode set to {current_movement_mode}")
        if current_movement_mode == 'auto_navigation':
            stop_event.clear()
            auto_nav_proc = Process(target=auto_navigation_process, args=(command_queue, client, stop_event))
            auto_nav_proc.start()
        else:
            stop_event.set()  # Stop auto-navigation
        return jsonify({"status": f"Mode set to {current_movement_mode}"})
    else:
        print("Invalid mode selected")
        return jsonify({"status": "Invalid mode selected"}), 400

# GEAR CONTROL -----------------------------------------------------
gear_command = 0    # Gear Command: 0(low), 1(mid), 2(high)

# ROUTE: /gear_low - Change motor's gear to low
@app.route("/gear_low", methods=['POST'])
def gear_low():
    global current_movement_mode

    # Package command
    gear_command = 0
    command_string = f"{gear_command}"
    
    # Send to Reciever
    if client.publish(MQTT_TOPIC_COMMAND, command_string):
        return jsonify({"Publish Status": "Gear Command Sent"})
    else:
        return jsonify({"Publish Status": "Gear Command Failed"}), 400
    
# ROUTE: /gear_mid - Change motor's gear to medium
@app.route("/gear_mid", methods=['POST'])
def gear_mid():
    global current_movement_mode

    # Package command
    gear_command = 1
    command_string = f"{gear_command}"
    
    # Send to Reciever
    if client.publish(MQTT_TOPIC_COMMAND, command_string):
        return jsonify({"Publish Status": "Gear Command Sent"})
    else:
        return jsonify({"Publish Status": "Gear Command Failed"}), 400

# ROUTE: /gear_low - Change motor's gear to high
@app.route("/gear_high", methods=['POST'])
def gear_high():
    global current_movement_mode

    # Package command
    gear_command = 2
    command_string = f"{gear_command}"
    
    # Send to Reciever
    if client.publish(MQTT_TOPIC_COMMAND, command_string):
        return jsonify({"Publish Status": "Gear Command Sent"})
    else:
        return jsonify({"Publish Status": "Gear Command Failed"}), 400
# ------------------------------------------------------------------

@app.route("/move_forward", methods=['POST'])
def move_forward():
    global current_movement_mode
    if current_movement_mode == 'basic_movement':
        front_back_command = 126  # Max forward
        side_side_command = 64    # Neutral steering
        command_string = f"{front_back_command} {side_side_command}"
        client.publish(MQTT_TOPIC_COMMAND, command_string)
        return jsonify({"status": "Moving forward"})
    else:
        return jsonify({"status": "Cannot move in current mode"}), 400
    
@app.route("/move_rail_forward", methods=['POST'])
def move_rail_forward():
    global current_movement_mode
    if current_movement_mode == 'basic_movement':
        move_command = 0  # Max forward
        command_string = f"{move_command}"
        client.publish(MQTT_RAIL_TOPIC_COMMAND, command_string)
        return jsonify({"status": "Moving rail forward"})
    else:
        return jsonify({"status": "Cannot move in current mode"}), 400

@app.route("/move_backward", methods=['POST'])
def move_backward():
    global current_movement_mode
    if current_movement_mode == 'basic_movement':
        front_back_command = 0   # Max backward
        side_side_command = 64    # Neutral steering
        command_string = f"{front_back_command} {side_side_command}"
        client.publish(MQTT_TOPIC_COMMAND, command_string)
        return jsonify({"status": "Moving backward"})
    else:
        return jsonify({"status": "Cannot move in current mode"}), 400

@app.route("/move_rail_backward", methods=['POST'])
def move_rail_backward():
    global current_movement_mode
    if current_movement_mode == 'basic_movement':
        move_command = 126   # Max backward
        command_string = f"{move_command}"
        client.publish(MQTT_RAIL_TOPIC_COMMAND, command_string)
        return jsonify({"status": "Moving rail backward"})
    else:
        return jsonify({"status": "Cannot move in current mode"}), 400

@app.route("/move_left", methods=['POST'])
def move_left():
    global current_movement_mode
    if current_movement_mode == 'basic_movement':
        front_back_command = 64  # Stop
        side_side_command = 126    # Max left
        command_string = f"{front_back_command} {side_side_command}"
        client.publish(MQTT_TOPIC_COMMAND, command_string)
        return jsonify({"status": "Turning left"})
    else:
        return jsonify({"status": "Cannot move in current mode"}), 400

@app.route("/move_right", methods=['POST'])
def move_right():
    global current_movement_mode
    if current_movement_mode == 'basic_movement':
        front_back_command = 64  # Stop
        side_side_command = 0   # Max right
        command_string = f"{front_back_command} {side_side_command}"
        client.publish(MQTT_TOPIC_COMMAND, command_string)
        return jsonify({"status": "Turning right"})
    else:
        return jsonify({"status": "Cannot move in current mode"}), 400

@app.route("/stop_robot", methods=['POST'])
def stop_robot():
    global current_movement_mode
    if current_movement_mode == 'basic_movement':
        front_back_command = 64   # Stop
        side_side_command = 64    # Neutral steering
        command_string = f"{front_back_command} {side_side_command}"
        client.publish(MQTT_TOPIC_COMMAND, command_string)
        return jsonify({"status": "Robot stopped"})
    else:
        return jsonify({"status": "Cannot stop in current mode"}), 400
    
@app.route("/stop_rail", methods=['POST'])
def stop_rail():
    global current_movement_mode
    if current_movement_mode == 'basic_movement':
        move_command = 64   # Stop
        command_string = f"{move_command}"
        client.publish(MQTT_RAIL_TOPIC_COMMAND, command_string)
        return jsonify({"status": "Rail stopped"})
    else:
        return jsonify({"status": "Cannot stop in current mode"}), 400

@app.route("/update_pid", methods=['POST'])
def update_pid():
    data = request.get_json()
    kp = data.get('kp')
    ki = data.get('ki')
    kd = data.get('kd')

    if kp is not None and ki is not None and kd is not None:
        # Send PID update to face_tracking process
        command_queue.put(('update_pid', (kp, ki, kd)))
        print(f"Updated PID parameters: Kp={kp}, Ki={ki}, Kd={kd}")
        return jsonify({"status": "PID parameters updated"})
    else:
        return jsonify({"status": "Invalid PID parameters"}), 400

@app.route('/send_coordinates', methods=['POST'])
def receive_coordinates():
    data = request.get_json()
    coordinates = data.get('coordinates', [])
    if coordinates:
        command_queue.put(('set_waypoints', coordinates))
        return jsonify({"status": "Coordinates received"})
    else:
        return jsonify({"status": "No coordinates received"}), 400
    
@app.route('/pump_on', methods=['POST'])
def pump_on():
    global current_movement_mode
    global current_pump_mode

    # Use for turning off AUTO MODE
    current_pump_mode = MANUAL_MODE
    # Use for turning off Pump
    if current_movement_mode == 'basic_movement':
        control_command = 1
        command_string = f"{control_command}"
        client.publish(MQTT_TOPIC_PUMP, command_string)
        return jsonify({"status": "Pump ON"})
    else:
        return jsonify({"status": "Cannot stop in current mode"}), 400
    
@app.route("/pump_off", methods=['POST'])
def pump_off():
    global current_movement_mode
    global current_pump_mode

    # Use for turning off AUTO MODE
    current_pump_mode = MANUAL_MODE
    # Use for turning off Pump
    if current_movement_mode == 'basic_movement':
        control_command = 0
        command_string = f"{control_command}"
        client.publish(MQTT_TOPIC_PUMP, command_string)
        return jsonify({"status": "Pump OFF"})
    else:
        return jsonify({"status": "Cannot stop in current mode"}), 400
    
@app.route("/pump_auto", methods=['POST'])
def pump_auto():
    global current_pump_mode
    current_pump_mode = AUTO_MODE
    return jsonify({"status": "Pump AUTO"})
        


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
    # Start the main loop thread
    t = threading.Thread(target=main_loop)
    t.daemon = True
    t.start()
    # Start face tracking process
    face_track_proc = Process(target=face_tracking_process, args=(command_queue, client))
    face_track_proc.start()
    # Run the Flask app
    app.run(host='0.0.0.0', port=5000)
