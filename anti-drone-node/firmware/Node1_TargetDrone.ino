/*
 * Node 1: Target Drone Simulator (The Threat)
 * 
 * Hardware: ESP32 + SX1278 LoRa Module
 * Goal: Simulates an invading drone broadcasting telemetry.
 * Action: Continuously transmit a dummy LoRa packet every 50 milliseconds.
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

// ESP32 SPI Pins for SX1278
#define LORA_SS 5
#define LORA_RST 14
#define LORA_DIO0 26

unsigned long previousMillis = 0;
const long transmitInterval = 50; // 50 milliseconds interval

void setup() {
  Serial.begin(115200);
  
  // Wait briefly for serial connection but do not block indefinitely
  unsigned long startWait = millis();
  while (!Serial && millis() - startWait < 3000) { }

  Serial.println("Starting Node 1: Target Drone Simulator");

  // Configure SPI pins
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  // Initialize LoRa at 915 MHz (Adjust to 433E6 or 866E6 based on local regulations)
  if (!LoRa.begin(915E6)) {
    Serial.println("Starting LoRa failed! Check wiring.");
    while (1); // Halt execution if initialization fails
  }

  Serial.println("Drone Simulator Ready. Broadcasting Telemetry...");
}

void loop() {
  unsigned long currentMillis = millis();

  // Strict non-blocking timing implementation using millis()
  if (currentMillis - previousMillis >= transmitInterval) {
    previousMillis = currentMillis;

    // Broadcast dummy telemetry packet
    LoRa.beginPacket();
    LoRa.print("DRONE_TELEMETRY_DATA");
    LoRa.endPacket();
  }
}
