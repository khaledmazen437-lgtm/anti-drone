// Connect to local Node.js backend
// We use the global 'io' variable provided by the CDN script in index.html
const socket = io('http://localhost:3000');

// UI Elements
const statusIndicator = document.getElementById('connection-indicator');
const statusText = document.getElementById('system-status-text');
const radarTarget = document.getElementById('radar-target');
const radarUI = document.getElementById('radar-ui');
const threatPanel = document.getElementById('threat-panel');
const threatTitle = document.getElementById('threat-title');
const rssiValue = document.getElementById('rssi-value');
const nodeStateValue = document.getElementById('node-state-value');
const btnJam = document.getElementById('btn-jam');
const btnStandby = document.getElementById('btn-standby');

// Connection Events
socket.on('connect', () => {
  statusIndicator.classList.add('online');
  statusText.innerText = 'SYSTEM ONLINE (CONNECTED)';
});

socket.on('disconnect', () => {
  statusIndicator.classList.remove('online');
  statusText.innerText = 'SYSTEM OFFLINE (DISCONNECTED)';
  resetUI();
});

// Telemetry Data Handler (Receives JSON from backend/ESP32)
socket.on('telemetry', (data) => {
  // data format: {"status": "SCANNING", "rssi": -75, "threat_detected": false}
  
  // Update RSSI
  rssiValue.innerText = `${data.rssi} dBm`;
  
  // Update Node State
  nodeStateValue.innerText = data.status;

  // Handle Threat State
  if (data.threat_detected) {
    // Show threat visually
    radarTarget.classList.remove('hidden');
    radarUI.classList.add('alert-mode');
    threatPanel.classList.add('alert');
    threatTitle.innerText = 'WARNING: THREAT DETECTED';
    
    // Dynamic positioning of the target dot based on RSSI (closer = stronger signal)
    // RSSI ranges roughly from -120 (far) to -30 (very close)
    let distancePercent = Math.max(0, Math.min(100, (data.rssi + 120) * 1.5));
    // Position randomly but closer to center based on signal strength
    const angle = Math.random() * Math.PI * 2;
    const radius = 50 - (distancePercent * 0.4); // 0 to 50% from center
    const x = 50 + radius * Math.cos(angle);
    const y = 50 + radius * Math.sin(angle);
    
    radarTarget.style.left = `${x}%`;
    radarTarget.style.top = `${y}%`;

  } else {
    radarTarget.classList.add('hidden');
    radarUI.classList.remove('alert-mode');
    threatPanel.classList.remove('alert');
    threatTitle.innerText = 'SCANNING AIRSPACE';
  }

  // Visual feedback for Jamming
  if (data.status === 'JAMMING') {
    threatTitle.innerText = 'JAMMER ENGAGED (RF FLOOD)';
    radarUI.classList.add('alert-mode'); // Keep red during jamming
  }
});

function resetUI() {
  rssiValue.innerText = '-- dBm';
  nodeStateValue.innerText = 'UNKNOWN';
  radarTarget.classList.add('hidden');
  radarUI.classList.remove('alert-mode');
  threatPanel.classList.remove('alert');
  threatTitle.innerText = 'CONNECTION LOST';
}

// Button Listeners
btnJam.addEventListener('click', () => {
  // Send command to backend -> ESP32
  socket.emit('command', 'ENGAGE_JAMMER');
});

btnStandby.addEventListener('click', () => {
  // Send command to backend -> ESP32
  socket.emit('command', 'STANDBY');
});
