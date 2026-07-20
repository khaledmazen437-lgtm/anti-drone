/*
 * Node 2: Radar Sensor & Jammer Node (The Defender)
 * 
 * Hardware: ESP32 + SX1278 LoRa Module
 * Goal: Scans for drone telemetry, communicates with PC via Serial (JSON),
 *       and executes jamming when commanded.
 *
 * ESP32 SPI Pin Definitions (Default VSPI):
 * SCK  (SPI Clock) -> GPIO 18
 * MISO (SPI MISO)  -> GPIO 19
 * MOSI (SPI MOSI)  -> GPIO 23
 * SS   (Chip Select)-> GPIO 5
 * RST  (Reset)     -> GPIO 14
 * DI0  (Interrupt) -> GPIO 26
 */

#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h> // Ensure ArduinoJson library (v6 or v7) is installed

// ESP32 SPI Pins for SX1278
#define LORA_SS 5
#define LORA_RST 14
#define LORA_DIO0 26

enum SystemState {
  STATE_SCANNING,
  STATE_JAMMING
};

SystemState currentState = STATE_SCANNING;
int currentRssi = -150; 
bool threatDetected = false;
const int THREAT_RSSI_THRESHOLD = -60;

unsigned long previousTelemetryMillis = 0;
const long telemetryInterval = 100; // 100ms update rate

String serialBuffer = "";

void setup() {
  // Serial communication set to 115200 baud rate
  Serial.begin(115200);
  
  // Wait briefly for serial connection but do not block indefinitely
  unsigned long startWait = millis();
  while (!Serial && millis() - startWait < 3000) { }

  // Configure SPI pins
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  // Initialize LoRa at 915 MHz (Adjust to 433E6 or 866E6 based on local regulations)
  if (!LoRa.begin(915E6)) {
    Serial.println("{\"error\": \"Starting LoRa failed! Check wiring.\"}");
    while (1); // Halt if initialization fails
  }

  // Set the module into receive mode initially
  LoRa.receive();
}

void processIncomingCommands() {
  // Non-blocking serial read character by character
  while (Serial.available() > 0) {
    char c = Serial.read();
    
    if (c == '\n') {
      // Parse complete JSON command
      // StaticJsonDocument provides compatibility for v6 and works similarly in v7
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, serialBuffer);

      if (!error && doc.containsKey("command")) {
        String cmd = doc["command"].as<String>();
        
        if (cmd == "ENGAGE_JAMMER") {
          currentState = STATE_JAMMING;
        } else if (cmd == "STANDBY") {
          currentState = STATE_SCANNING;
          currentRssi = -150; // Reset RSSI on standby
          threatDetected = false;
          LoRa.receive(); // Ensure we go back to listening mode
        }
      }
      serialBuffer = ""; // Reset buffer after parsing
    } else if (c != '\r') {
      serialBuffer += c;
      // Prevent buffer overflow in case of malformed input without newlines
      if (serialBuffer.length() > 512) {
        serialBuffer = "";
      }
    }
  }
}

void loop() {
  unsigned long currentMillis = millis();
  
  // 1. Process any incoming JSON commands from PC (Strictly non-blocking)
  processIncomingCommands();

  // 2. State Machine Execution
  if (currentState == STATE_SCANNING) {
    // Check for incoming LoRa packets non-blockingly
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      // Extract absolute RSSI from the packet
      currentRssi = LoRa.packetRssi();
      
      // Determine threat level based on RSSI threshold
      if (currentRssi > THREAT_RSSI_THRESHOLD) {
        threatDetected = true;
      } else {
        threatDetected = false;
      }
    }
  } else if (currentState == STATE_JAMMING) {
    // Spam the frequency with random bytes as rapidly as possible to jam
    LoRa.beginPacket();
    for (int i = 0; i < 64; i++) {
      LoRa.write(random(256));
    }
    LoRa.endPacket();
  }

  // 3. PC Communication: Send JSON telemetry every 100ms
  if (currentMillis - previousTelemetryMillis >= telemetryInterval) {
    previousTelemetryMillis = currentMillis;

    // Use StaticJsonDocument for widespread compatibility
    StaticJsonDocument<256> txDoc;
    txDoc["status"] = (currentState == STATE_SCANNING) ? "SCANNING" : "JAMMING";
    txDoc["rssi"] = currentRssi;
    txDoc["threat_detected"] = threatDetected;

    serializeJson(txDoc, Serial);
    Serial.println(); // Send newline to terminate JSON payload
  }
}
