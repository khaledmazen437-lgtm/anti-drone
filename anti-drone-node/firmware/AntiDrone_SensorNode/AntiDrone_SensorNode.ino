/*
 * ============================================================================
 *  ITC-EGYPT  |  SOLID-STATE ANTI-DRONE TACTICAL SENSOR NODE
 * ============================================================================
 *  Platform      : ESP32 (Arduino Core)
 *  RF Front-End  : AS32-TTL-100 LoRa Module (UART transparent mode)
 *  Link to PC    : USB Serial (115200 baud) <-> Node.js / React Dashboard
 *
 *  ARCHITECTURE
 *  ------------
 *  This firmware is a fully non-blocking finite state machine (FSM) with
 *  three states:
 *
 *      SCANNING  -> passively listens to Serial2 (LoRa RX), measures RF
 *                   "activity" (byte throughput) and derives a simulated
 *                   energy level (0-100).
 *      ALERT     -> entered automatically when a sudden statistical spike
 *                   in RF activity is detected (see energy-spike detector
 *                   below). This models a drone's control/video-link burst.
 *      JAMMING   -> entered on command from the PC. Floods Serial2 TX with
 *                   pseudo-random garbage bytes continuously (Spot Jamming
 *                   simulation) with no gaps, but still non-blocking.
 *
 *  NO delay() IS USED ANYWHERE. All timing is done with millis()-based
 *  schedulers so the loop() never stalls and stays responsive to both the
 *  LoRa UART and the PC UART simultaneously.
 *
 *  DEPENDENCIES
 *  -------------
 *   - ArduinoJson (v6.x or v7.x)  ->  Sketch > Include Library > Manage Libraries
 * ============================================================================
 */

#include <ArduinoJson.h>

// ============================================================================
// HARDWARE PIN CONFIGURATION
// ============================================================================
#define LORA_RX_PIN        16      // ESP32 GPIO16 (RX2)  <- AS32 TXD
#define LORA_TX_PIN        17      // ESP32 GPIO17 (TX2)  -> AS32 RXD
#define LORA_BAUD          9600    // Default AS32-TTL-100 transparent-mode baud.
                                    // Must match the module's DIP/AT config.
#define PC_BAUD            115200  // USB Serial to laptop dashboard

// ============================================================================
// TIMING CONSTANTS (all non-blocking, millis()-based)
// ============================================================================
const unsigned long TELEMETRY_INTERVAL_MS   = 100;   // JSON push rate to PC
const unsigned long ENERGY_WINDOW_MS        = 100;   // RF sampling window
const unsigned long JAM_TX_INTERVAL_MS      = 4;      // garbage byte burst period while jamming
const unsigned long ALERT_HOLD_MS           = 2000;   // how long ALERT stays latched after last spike

// ============================================================================
// ENERGY / SPIKE-DETECTION TUNING
// ============================================================================
// MAX_BYTES_PER_WINDOW defines what we consider "100% energy" -> i.e. the
// AS32 UART saturated with incoming bytes during one ENERGY_WINDOW_MS slice.
// At 9600 baud, ~960 bytes/sec is the physical ceiling -> ~96 bytes/100ms.
// We clamp this a bit lower to leave headroom for mapping.
const uint16_t MAX_BYTES_PER_WINDOW = 90;

// Baseline EMA (Exponential Moving Average) smoothing factor.
// Lower alpha = baseline adapts slowly = more stable "noise floor".
const float BASELINE_ALPHA = 0.10f;

// A spike is flagged when instantaneous energy exceeds the adaptive
// baseline by this multiplier AND clears an absolute noise floor.
const float SPIKE_MULTIPLIER      = 2.5f;
const uint8_t SPIKE_MIN_ABS_LEVEL = 15;   // ignore spikes below this absolute level (noise immunity)

// ============================================================================
// SYSTEM STATE
// ============================================================================
enum SystemState : uint8_t {
  STATE_SCANNING = 0,
  STATE_ALERT    = 1,
  STATE_JAMMING  = 2
};

SystemState currentState = STATE_SCANNING;

// --- RF activity accounting -------------------------------------------------
volatile uint16_t rxByteCounter   = 0;     // bytes seen on Serial2 in current window
uint8_t  energyLevel              = 0;     // 0-100, published to PC
float    baselineEnergy           = 0.0f;  // adaptive noise floor (EMA)
bool     threatDetected           = false; // true while latched in ALERT
unsigned long lastAlertTriggerMs  = 0;     // last time a spike was seen (for hold timer)

// --- Scheduler timestamps ---------------------------------------------------
unsigned long lastTelemetryMs = 0;
unsigned long lastEnergyCalcMs = 0;
unsigned long lastJamTxMs = 0;

// --- PC command line buffer --------------------------------------------------
String pcInputBuffer = "";
const size_t PC_BUFFER_MAX = 256; // guard against runaway/garbage input

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(PC_BAUD);                                   // USB link to dashboard
  Serial2.begin(LORA_BAUD, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN); // AS32 link

  randomSeed(analogRead(34)); // GPIO34 is input-only/floating -> decent entropy source
                              // for the jammer's pseudo-random payload

  pcInputBuffer.reserve(PC_BUFFER_MAX);

  // Small startup banner on the LoRa side is optional; keep silent to stay
  // in pure passive-listening posture on boot.
}

// ============================================================================
// MAIN LOOP  (non-blocking dispatcher)
// ============================================================================
void loop() {
  drainLoRaRxBuffer();      // always count incoming LoRa bytes (RF activity)
  handlePcCommands();       // always listen for PC JSON commands
  updateEnergyAndDetectSpike(); // periodic RF energy computation + anomaly check
  runJammerIfActive();      // continuous garbage TX while in JAMMING state
  publishTelemetry();       // periodic JSON push to PC
  updateAlertTimeout();     // auto-clear ALERT after hold window if quiet
}

// ============================================================================
// 1) DRAIN LORA RX BUFFER
// ----------------------------------------------------------------------------
// We don't care about the *content* of the received bytes (the AS32 is in
// transparent mode and we have no real drone-signature demodulator here).
// What we DO care about is *how much* data arrives and *how fast* — a burst
// of bytes in a short window is our proxy for an RF energy spike, exactly
// like a spectrum analyzer would show a power spike from a drone's
// telemetry/video uplink keying up nearby.
// ============================================================================
void drainLoRaRxBuffer() {
  // Never count RX bytes while actively jamming: our own TX flood would
  // otherwise self-trigger false "spikes" (and half-duplex AS32 modules may
  // echo/reflect noise back on the RX line while transmitting).
  if (currentState == STATE_JAMMING) {
    while (Serial2.available()) Serial2.read(); // still flush, but don't count
    return;
  }

  while (Serial2.available()) {
    Serial2.read();      // discard payload content
    rxByteCounter++;      // but tally volume for the energy calculation
  }
}

// ============================================================================
// 2) ENERGY CALCULATION + SPIKE (ANOMALY) DETECTION
// ----------------------------------------------------------------------------
// Every ENERGY_WINDOW_MS we convert the raw byte count into a 0-100 "energy
// level" and compare it against an adaptive baseline (EMA of recent energy).
//
//   spike condition:  energyLevel > baseline * SPIKE_MULTIPLIER
//                      AND energyLevel >= SPIKE_MIN_ABS_LEVEL
//
// The multiplier makes detection *relative* to the current RF noise floor
// (so a naturally noisy environment doesn't set off constant false alarms),
// while the absolute floor prevents tiny baseline values (e.g. baseline≈1)
// from making the detector hyper-sensitive to normal jitter.
// ============================================================================
void updateEnergyAndDetectSpike() {
  unsigned long now = millis();
  if (now - lastEnergyCalcMs < ENERGY_WINDOW_MS) return;
  lastEnergyCalcMs = now;

  // Snapshot and reset the byte counter atomically enough for this single-core loop context
  uint16_t bytesThisWindow = rxByteCounter;
  rxByteCounter = 0;

  // Map raw byte volume -> 0-100 scale
  long mapped = map(bytesThisWindow, 0, MAX_BYTES_PER_WINDOW, 0, 100);
  energyLevel = (uint8_t)constrain(mapped, 0, 100);

  // Don't run spike detection while we're actively jamming (no meaningful RX picture)
  if (currentState != STATE_JAMMING) {
    bool isSpike = (energyLevel > (baselineEnergy * SPIKE_MULTIPLIER)) &&
                   (energyLevel >= SPIKE_MIN_ABS_LEVEL);

    if (isSpike) {
      currentState = STATE_ALERT;
      threatDetected = true;
      lastAlertTriggerMs = now;
    }

    // Update the adaptive baseline with an EMA. We intentionally keep
    // updating it even during ALERT so the system re-calibrates to a new
    // ambient level rather than staying permanently trigger-happy.
    baselineEnergy = (baselineEnergy * (1.0f - BASELINE_ALPHA)) +
                      ((float)energyLevel * BASELINE_ALPHA);
  }
}

// ============================================================================
// AUTO-CLEAR ALERT STATE
// ----------------------------------------------------------------------------
// ALERT is "latched" for ALERT_HOLD_MS after the last detected spike. If no
// new spike occurs within that window, we drop back to SCANNING. This
// avoids flickering between SCANNING/ALERT on every single sample.
// ============================================================================
void updateAlertTimeout() {
  if (currentState == STATE_ALERT) {
    if (millis() - lastAlertTriggerMs >= ALERT_HOLD_MS) {
      currentState = STATE_SCANNING;
      threatDetected = false;
    }
  }
}

// ============================================================================
// 3) JAMMER — continuous non-blocking garbage flood over Serial2
// ----------------------------------------------------------------------------
// Simulates Spot Jamming: as fast as JAM_TX_INTERVAL_MS allows, we push a
// random byte out over the LoRa UART. This is spaced with millis() rather
// than blasted in a tight while() loop so the ESP32 remains fully
// responsive to PC commands (e.g. an immediate STANDBY abort).
// ============================================================================
void runJammerIfActive() {
  if (currentState != STATE_JAMMING) return;

  unsigned long now = millis();
  if (now - lastJamTxMs < JAM_TX_INTERVAL_MS) return;
  lastJamTxMs = now;

  // Send a small burst per tick to maximize duty cycle without ever
  // blocking on Serial2.flush()/delay().
  const uint8_t burstSize = 8;
  for (uint8_t i = 0; i < burstSize; i++) {
    Serial2.write((uint8_t)random(0, 256));
  }
}

// ============================================================================
// 4) TELEMETRY PUBLISHER — JSON to PC every 100ms
// ----------------------------------------------------------------------------
// Format: {"status":"SCANNING","energy_level":45,"threat_detected":false}
// ============================================================================
void publishTelemetry() {
  unsigned long now = millis();
  if (now - lastTelemetryMs < TELEMETRY_INTERVAL_MS) return;
  lastTelemetryMs = now;

  StaticJsonDocument<128> doc;
  doc["status"] = stateToString(currentState);
  doc["energy_level"] = energyLevel;
  doc["threat_detected"] = threatDetected;

  serializeJson(doc, Serial);
  Serial.println(); // newline-delimited JSON so the Node.js side can readline()
}

const char* stateToString(SystemState s) {
  switch (s) {
    case STATE_SCANNING: return "SCANNING";
    case STATE_ALERT:    return "ALERT";
    case STATE_JAMMING:  return "JAMMING";
    default:             return "UNKNOWN";
  }
}

// ============================================================================
// 5) PC COMMAND LISTENER
// ----------------------------------------------------------------------------
// Reads Serial byte-by-byte (non-blocking), accumulates a line until '\n',
// then parses it as JSON. Expected payloads:
//    {"command":"ENGAGE_JAMMER"}
//    {"command":"STANDBY"}
// ============================================================================
void handlePcCommands() {
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == '\n') {
      processPcLine(pcInputBuffer);
      pcInputBuffer = "";
    } else if (c != '\r') {
      if (pcInputBuffer.length() < PC_BUFFER_MAX) {
        pcInputBuffer += c;
      } else {
        // Overflow guard: drop and reset to avoid unbounded String growth
        pcInputBuffer = "";
      }
    }
  }
}

void processPcLine(const String &line) {
  if (line.length() == 0) return;

  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    // Malformed JSON from the dashboard — ignore silently to stay resilient
    return;
  }

  const char* command = doc["command"] | "";

  if (strcmp(command, "ENGAGE_JAMMER") == 0) {
    currentState = STATE_JAMMING;
    threatDetected = true; // jamming implies an active engagement/threat context
  } else if (strcmp(command, "STANDBY") == 0) {
    // Return to passive listening posture and reset detection state
    currentState = STATE_SCANNING;
    threatDetected = false;
    rxByteCounter = 0;
    baselineEnergy = 0.0f;
    energyLevel = 0;
  }
  // Unknown commands are ignored (extend here for future commands, e.g. RESET)
}
