const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');

const app = express();
const server = http.createServer(app);
const io = new Server(server, {
  cors: {
    origin: '*',
    methods: ['GET', 'POST']
  }
});

const PORT = 3000;

// === IMPORTANT: SET YOUR COM PORT HERE ===
// Examples: 'COM3' (Windows), '/dev/ttyUSB0' (Linux/Mac)
// Make sure this matches the port your Node 2 ESP32 is connected to
const SERIAL_PORT_PATH = 'COM3'; 
const BAUD_RATE = 115200;

let port;
let parser;

function connectSerial() {
  console.log(`[Serial] Attempting to connect to ${SERIAL_PORT_PATH}...`);
  
  port = new SerialPort({ path: SERIAL_PORT_PATH, baudRate: BAUD_RATE }, (err) => {
    if (err) {
      console.error(`[Serial] Error opening port: ${err.message}`);
      console.log('[Serial] Is the ESP32 connected? Waiting 5 seconds before retrying...');
      setTimeout(connectSerial, 5000);
    }
  });

  parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));

  port.on('open', () => {
    console.log(`[Serial] Successfully opened ${SERIAL_PORT_PATH}`);
  });

  // Read data from ESP32 Node 2
  parser.on('data', (data) => {
    try {
      const parsedData = JSON.parse(data.trim());
      // Broadcast telemetry to all connected frontend clients (the dashboard)
      io.emit('telemetry', parsedData);
    } catch (e) {
      // Ignored non-JSON debug strings that ESP32 might send
      console.log(`[Serial] Raw data received: ${data.trim()}`);
    }
  });

  port.on('close', () => {
    console.log('[Serial] Port closed. Reconnecting in 5 seconds...');
    setTimeout(connectSerial, 5000);
  });
}

connectSerial();

// Handle WebSocket connections from the Frontend Dashboard
io.on('connection', (socket) => {
  console.log(`[WebSocket] Dashboard Client connected: ${socket.id}`);

  // Receive commands from Frontend and send to ESP32
  socket.on('command', (cmd) => {
    console.log(`[WebSocket] Received command from UI: ${cmd}`);
    if (port && port.isOpen) {
      // Format must match what Node 2 expects: {"command": "ENGAGE_JAMMER"}
      const payload = JSON.stringify({ command: cmd }) + '\n';
      port.write(payload, (err) => {
        if (err) {
          console.error('[Serial] Error writing command to ESP32:', err.message);
        } else {
          console.log(`[Serial] Command Sent to ESP32: ${payload.trim()}`);
        }
      });
    } else {
      console.log('[Serial] Cannot send command, port is not open.');
    }
  });

  socket.on('disconnect', () => {
    console.log(`[WebSocket] Dashboard Client disconnected: ${socket.id}`);
  });
});

server.listen(PORT, () => {
  console.log(`\n======================================================`);
  console.log(`🚀 Anti-Drone Backend running on http://localhost:${PORT}`);
  console.log(`======================================================\n`);
});
