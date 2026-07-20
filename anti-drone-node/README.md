# Anti-Drone Tactical Sensor Node — ITC-Egypt

Solid-state (no moving parts) anti-drone threat detector & jammer built on an
ESP32 + AS32-TTL-100 LoRa module, with a live Node.js backend and React
dashboard for monitoring and control.

```
anti-drone-node/
├── firmware/            # ESP32 Arduino sketch (flash via Arduino IDE)
│   └── AntiDrone_SensorNode/
│       └── AntiDrone_SensorNode.ino
├── backend/              # Node.js/Express server: bridges Serial <-> WebSocket
│   ├── server.js
│   ├── package.json
│   └── .env.example
└── frontend/             # React (Vite) live dashboard
    ├── src/
    ├── package.json
    └── index.html
```

## How it all connects

```
[Drone RF] --> [AS32-TTL-100] --UART--> [ESP32] --USB Serial--> [Node.js backend] --WebSocket--> [React dashboard]
```

The ESP32 pushes a JSON telemetry line every 100ms over USB Serial:
```json
{"status":"SCANNING","energy_level":45,"threat_detected":false}
```

The backend reads that serial stream, re-broadcasts it over Socket.IO to any
connected browser dashboard, and exposes a REST endpoint the dashboard uses to
send commands back down to the ESP32:
```json
{"command":"ENGAGE_JAMMER"}
{"command":"STANDBY"}
```

## 1) Flash the firmware

1. Open `firmware/AntiDrone_SensorNode/AntiDrone_SensorNode.ino` in Arduino IDE.
2. Install the **ArduinoJson** library (Sketch → Include Library → Manage Libraries).
3. Select your ESP32 board + correct COM port, then Upload.
4. Wire the AS32-TTL-100 as described in that sketch's header comments
   (GPIO16 = RX2, GPIO17 = TX2, M0/M1 to GND, shared GND, dedicated power rail).

## 2) Run the backend

```bash
cd backend
cp .env.example .env      # then edit SERIAL_PORT to match your OS (see below)
npm install
npm start
```

Finding your serial port name:
- **Windows**: Device Manager → Ports (COM & LPT) → e.g. `COM5`
- **macOS**: `ls /dev/tty.*` → e.g. `/dev/tty.usbserial-0001`
- **Linux**: `ls /dev/ttyUSB*` → e.g. `/dev/ttyUSB0`

The backend starts on `http://localhost:4000` by default and:
- Broadcasts live telemetry over Socket.IO (event: `telemetry`)
- Accepts commands at `POST /api/command` with body `{"command":"ENGAGE_JAMMER"}`

## 3) Run the dashboard

```bash
cd frontend
npm install
npm run dev
```

Open the printed local URL (default `http://localhost:5173`). The dashboard
connects to the backend automatically and shows live status, energy level,
and threat indication, with buttons to engage/disengage the jammer.

## Notes for the competition demo

- If the ESP32 isn't plugged in yet, the backend will keep retrying the
  serial connection and log a warning — it won't crash.
- Only one process can hold the serial port at a time — close the Arduino
  IDE's Serial Monitor before starting the backend.
