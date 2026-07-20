/**
 * ============================================================================
 *  ANTI-DRONE SENSOR NODE — BACKEND BRIDGE
 * ============================================================================
 *  Responsibilities:
 *    1. Open the USB serial connection to the ESP32.
 *    2. Parse newline-delimited JSON telemetry lines coming from the ESP32
 *       and re-broadcast them to all connected dashboard clients via
 *       Socket.IO (event: "telemetry").
 *    3. Expose a REST endpoint (POST /api/command) the dashboard uses to
 *       send commands (ENGAGE_JAMMER / STANDBY) back down to the ESP32.
 *    4. Auto-reconnect to the serial port if the ESP32 is unplugged/replugged,
 *       instead of crashing the whole backend.
 * ============================================================================
 */

require("dotenv").config();

const express = require("express");
const cors = require("cors");
const http = require("http");
const { Server } = require("socket.io");
const { SerialPort } = require("serialport");
const { ReadlineParser } = require("@serialport/parser-readline");

const PORT = process.env.PORT || 4000;
const SERIAL_PORT_NAME = process.env.SERIAL_PORT || "COM5";
const BAUD_RATE = parseInt(process.env.BAUD_RATE || "115200", 10);
const CORS_ORIGIN = process.env.CORS_ORIGIN || "http://localhost:5173";
const SERIAL_RECONNECT_DELAY_MS = 3000;

// ----------------------------------------------------------------------------
// Express + Socket.IO setup
// ----------------------------------------------------------------------------
const app = express();
app.use(cors({ origin: CORS_ORIGIN }));
app.use(express.json());

const httpServer = http.createServer(app);
const io = new Server(httpServer, {
  cors: { origin: CORS_ORIGIN, methods: ["GET", "POST"] },
});

// Keep the most recent telemetry snapshot so newly-connected dashboards
// get an immediate state instead of waiting up to 100ms for the next line.
let lastTelemetry = {
  status: "DISCONNECTED",
  energy_level: 0,
  threat_detected: false,
};

io.on("connection", (socket) => {
  console.log(`[socket.io] dashboard connected: ${socket.id}`);
  socket.emit("telemetry", lastTelemetry);

  socket.on("disconnect", () => {
    console.log(`[socket.io] dashboard disconnected: ${socket.id}`);
  });
});

// ----------------------------------------------------------------------------
// Serial connection to the ESP32 (with auto-reconnect)
// ----------------------------------------------------------------------------
let serialPort = null;

function connectSerial() {
  serialPort = new SerialPort(
    { path: SERIAL_PORT_NAME, baudRate: BAUD_RATE },
    (err) => {
      if (err) {
        console.error(`[serial] failed to open ${SERIAL_PORT_NAME}: ${err.message}`);
        scheduleReconnect();
      }
    }
  );

  const parser = serialPort.pipe(new ReadlineParser({ delimiter: "\n" }));

  serialPort.on("open", () => {
    console.log(`[serial] connected to ESP32 on ${SERIAL_PORT_NAME} @ ${BAUD_RATE} baud`);
  });

  parser.on("data", (line) => {
    const trimmed = line.trim();
    if (!trimmed) return;

    try {
      const telemetry = JSON.parse(trimmed);
      lastTelemetry = telemetry;
      io.emit("telemetry", telemetry);
    } catch (e) {
      // Non-JSON noise (e.g. boot logs) — ignore rather than crash the bridge
      console.warn(`[serial] ignored non-JSON line: ${trimmed}`);
    }
  });

  serialPort.on("error", (err) => {
    console.error(`[serial] error: ${err.message}`);
  });

  serialPort.on("close", () => {
    console.warn("[serial] port closed — will attempt to reconnect");
    lastTelemetry = { status: "DISCONNECTED", energy_level: 0, threat_detected: false };
    io.emit("telemetry", lastTelemetry);
    scheduleReconnect();
  });
}

function scheduleReconnect() {
  setTimeout(connectSerial, SERIAL_RECONNECT_DELAY_MS);
}

connectSerial();

// ----------------------------------------------------------------------------
// REST endpoint: dashboard -> ESP32 commands
// ----------------------------------------------------------------------------
app.post("/api/command", (req, res) => {
  const { command } = req.body;

  const allowedCommands = ["ENGAGE_JAMMER", "STANDBY"];
  if (!allowedCommands.includes(command)) {
    return res.status(400).json({ error: `Invalid command. Allowed: ${allowedCommands.join(", ")}` });
  }

  if (!serialPort || !serialPort.isOpen) {
    return res.status(503).json({ error: "ESP32 serial port is not connected" });
  }

  const payload = JSON.stringify({ command }) + "\n";
  serialPort.write(payload, (err) => {
    if (err) {
      console.error(`[serial] write failed: ${err.message}`);
      return res.status(500).json({ error: "Failed to write to serial port" });
    }
    console.log(`[serial] -> ESP32: ${payload.trim()}`);
    res.json({ success: true, sent: command });
  });
});

app.get("/api/health", (req, res) => {
  res.json({
    serialConnected: !!(serialPort && serialPort.isOpen),
    serialPortName: SERIAL_PORT_NAME,
    lastTelemetry,
  });
});

httpServer.listen(PORT, () => {
  console.log(`[backend] listening on http://localhost:${PORT}`);
});
