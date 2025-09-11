var map;
var robotMarker;
var robotIcon;
var drawnItems;
var drawControl;
var pathPolyline = null;
var plannedPath = null;
var gpsData = [];

function fetchMoistureValue() {
fetch('/get_moistValue')
    .then(response => response.json())
    .then(data => {
        document.getElementById('moisture-value').textContent = data.moisture_value;
    })
    .catch(error => {
        console.error('Error fetching moisture value:', error);
    });
}
function updateMoistThreshold() {
    var threshold = parseFloat(document.getElementById('threshold').value);

    fetch('/update_MoistThreshold', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(threshold)
    })
    .then(response => response.json())
    .then(data => {
        alert('Threshold updated');
    })
    .catch(error => console.error('Error:', error));
}

// Fetch immediately, then every 5 seconds
fetchMoistureValue();
setInterval(fetchMoistureValue, 5000);

// Emergency Controls
function sendEStop() {
    fetch('/estop', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
        alert(data.status);
    })
    .catch(error => console.error('Error:', error));
}

function undoEStop() {
    fetch('/undo_estop', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
        alert(data.status);
    })
    .catch(error => console.error('Error:', error));
}

// Robot Movement Controls
function moveForward() {
    fetch('/move_forward', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
        console.log('Robot moving forward');
    })
    .catch(error => console.error('Error:', error));
}

function moveRailForward() {
    fetch('/move_rail_forward', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
        console.log('Rail going forward');
    })
    .catch(error => console.error('Error:', error));
}

function moveBackward() {
    fetch('/move_backward', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
        console.log('Robot moving backward');
    })
    .catch(error => console.error('Error:', error));
}

function moveRailBackward() {
    fetch('/move_rail_backward', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
        console.log('Rail going backward');
    })
    .catch(error => console.error('Error:', error));
}

function moveLeft() {
    fetch('/move_left', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
        console.log('Robot moving left');
    })
    .catch(error => console.error('Error:', error));
}

function moveRight() {
    fetch('/move_right', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
        console.log('Robot moving right');
    })
    .catch(error => console.error('Error:', error));
}

function stopRobot() {
    fetch('/stop_robot', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
        console.log('Robot stopped');
    })
    .catch(error => console.error('Error:', error));
}

function stopRail() {
    fetch('/stop_rail', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
        console.log('Rail stopped');
    })
    .catch(error => console.error('Error:', error));
}

// Pump Settings
function pumpON() {
    fetch('/pump_on', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
        console.log('Turning Pump ON')
    })
    .catch(error => console.error('Error:', error));
}
    
function pumpOFF() {
    fetch('/pump_off', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
        console.log('Turning Pump OFF')
    })
    .catch(error => console.error('Error:', error));
}

function pumpAUTO() {
    fetch('/pump_auto', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
        console.log('Pump is now automatic')
    })
    .catch(error => console.error('Error:', error));
}

// Face Detection Settings
function increaseFaceArea() {
    fetch('/increase_face_area', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
        document.getElementById('desiredFaceArea').value = data.desired_face_area;
    })
    .catch(error => console.error('Error:', error));
}

function decreaseFaceArea() {
    fetch('/decrease_face_area', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
        document.getElementById('desiredFaceArea').value = data.desired_face_area;
    })
    .catch(error => console.error('Error:', error));
}

function moveCenterLeft() {
    fetch('/move_center_left', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
        document.getElementById('centerOffset').value = data.center_offset;
    })
    .catch(error => console.error('Error:', error));
}

function moveCenterRight() {
    fetch('/move_center_right', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
        document.getElementById('centerOffset').value = data.center_offset;
    })
    .catch(error => console.error('Error:', error));
}

// PID Controller Settings
function updatePID() {
    var kp = parseFloat(document.getElementById('kp').value);
    var ki = parseFloat(document.getElementById('ki').value);
    var kd = parseFloat(document.getElementById('kd').value);

    var data = { kp: kp, ki: ki, kd: kd };

    fetch('/update_pid', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(data)
    })
    .then(response => response.json())
    .then(data => {
        alert('PID parameters updated');
    })
    .catch(error => console.error('Error:', error));
}

// Mode Selection
function setMode(mode) {
    fetch('/set_mode', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ mode: mode })
    })
    .then(response => response.json())
    .then(data => {
        alert('Mode set to ' + mode.replace('_', ' '));
        if (mode === 'auto_navigation') {
            document.getElementById('autoNavControls').style.display = 'block';
            initializeDrawing();
        } else {
            document.getElementById('autoNavControls').style.display = 'none';
            removeDrawing();
        }
    })
    .catch(error => console.error('Error:', error));
}

function initializeDrawing() {
    drawnItems = new L.FeatureGroup();
    map.addLayer(drawnItems);

    drawControl = new L.Control.Draw({
        edit: {
            featureGroup: drawnItems
        }
    });
    map.addControl(drawControl);

    map.on(L.Draw.Event.CREATED, function (event) {
        var layer = event.layer;
        drawnItems.addLayer(layer);
    });

    document.getElementById('sendButton').onclick = function() {
        var coordinates = [];
        drawnItems.eachLayer(function(layer) {
            if (layer instanceof L.Polyline || layer instanceof L.Polygon) {
                var latLngs = layer.getLatLngs();
                if (latLngs.length > 0 && Array.isArray(latLngs[0])) {
                    latLngs.forEach(function(latlngArray) {
                        latlngArray.forEach(function(latlng) {
                            coordinates.push({ lat: latlng.lat, lng: latlng.lng });
                        });
                    });
                } else {
                    latLngs.forEach(function(latlng) {
                        coordinates.push({ lat: latlng.lat, lng: latlng.lng });
                    });
                }
            } else if (layer instanceof L.Marker) {
                var latlng = layer.getLatLng();
                coordinates.push({ lat: latlng.lat, lng: latlng.lng });
            }
        });

        fetch('/send_coordinates', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ coordinates: coordinates })
        })
        .then(response => response.json())
        .then(data => {
            console.log(data);
            alert("Coordinates sent to the server");
            if (plannedPath) {
                map.removeLayer(plannedPath);
            }
            plannedPath = L.polyline(coordinates.map(c => [c.lat, c.lng]), {color: 'green'}).addTo(map);
        })
        .catch(error => console.error('Error:', error));
    };

    document.getElementById('estopButton').onclick = function() {
        sendEStop();
    };

    document.getElementById('undoEstopButton').onclick = function() {
        undoEStop();
    };
}

function removeDrawing() {
    if (drawControl) {
        map.removeControl(drawControl);
        drawControl = null;
    }
    if (drawnItems) {
        map.removeLayer(drawnItems);
        drawnItems = null;
    }
    if (plannedPath) {
        map.removeLayer(plannedPath);
        plannedPath = null;
    }
    if (pathPolyline) {
        map.removeLayer(pathPolyline);
        pathPolyline = null;
    }
}

// Initialize the map
map = L.map('map').setView([0, 0], 2);

// Add OpenStreetMap tile layer
L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    attribution: '&copy; OpenStreetMap contributors'
}).addTo(map);

// Robot marker with rotation
robotIcon = L.icon({
    iconUrl: 'https://raw.githubusercontent.com/pointhi/leaflet-color-markers/master/img/marker-icon-red.png',
    iconSize: [25, 41],
    iconAnchor: [12, 41],
});

robotMarker = L.marker([0, 0], {
    icon: robotIcon,
    rotationAngle: 0
}).addTo(map);

// Fetch initial GPS position
fetch('/initial_gps')
    .then(response => response.json())
    .then(data => {
        var initialLat = data.lat || 0;
        var initialLon = data.lon || 0;
        map.setView([initialLat, initialLon], 18);
        robotMarker.setLatLng([initialLat, initialLon]);
    });

// Update robot position and heading periodically
setInterval(function() {
    fetch('/get_gps_data')
        .then(response => response.json())
        .then(data => {
            if (data && data.length > 0) {
                gpsData = data;
                var gpsCoordinates = data.map(function(point) {
                    return [point.Estimated_Lat || point.GPS_Lat, point.Estimated_Lon || point.GPS_Lon];
                });

                var latestPosition = gpsCoordinates[gpsCoordinates.length - 1];
                robotMarker.setLatLng(latestPosition);

                var heading = data[data.length - 1].Estimated_Theta || data[data.length - 1].Heading || 0;
                robotMarker.setRotationAngle(heading);

                if (pathPolyline) {
                    pathPolyline.setLatLngs(gpsCoordinates);
                } else {
                    pathPolyline = L.polyline(gpsCoordinates, {color: 'blue'}).addTo(map);
                }
            }
        })
        .catch(error => console.error('Error fetching GPS data:', error));
}, 1000);