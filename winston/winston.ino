#include "BigBrotherComms.h"

// --- Network Configuration ---
const char* ssid = "";
const char* password = "";
IPAddress server_ip(0, 0, 0, 0); 
const int server_port = 8080;

// Initialize as DEVICE_THERMOSTAT (0x03)
BigBrotherComms comms(ssid, password, server_ip, server_port, DEVICE_THERMOSTAT);

// --- Hardware Pins --- 
// Inputs from a physical wall thermostat (pulled high, switch to ground)
const int heatRequestPin = 32; 
const int coolRequestPin = 33; 

// Outputs to the actual HVAC unit relays
const int hvacHeatRelay = 26; 
const int hvacCoolRelay = 27; 

// --- State Variables ---
bool heatRequested = false;
bool coolRequested = false;
unsigned long reportTimer = 0;

void setup() {
  Serial.begin(115200);

  comms.begin(); 

  // Configure Inputs
  pinMode(heatRequestPin, INPUT_PULLUP);
  pinMode(coolRequestPin, INPUT_PULLUP);

  // Configure Outputs
  pinMode(hvacHeatRelay, OUTPUT);
  pinMode(hvacCoolRelay, OUTPUT);
  digitalWrite(hvacHeatRelay, HIGH);
  digitalWrite(hvacCoolRelay, HIGH);
}

void loop() {
  // STATE: Handshaking
  if (!comms.isAssigned()) {
    if (!comms.handleHandshake()) {
      delay(1000);
    }
  } 
  // STATE: Normal Operation
  else {
    // 1. Read Inputs & Send Data (Every 5 seconds)
    if (millis() - reportTimer > 5000) {
      // Assuming LOW means the physical thermostat switch is closed
      heatRequested = (digitalRead(heatRequestPin) == LOW);
      coolRequested = (digitalRead(coolRequestPin) == LOW);

      Serial.println(digitalRead(coolRequestPin) == LOW);

      comms.sendThermostatData(heatRequested, coolRequested);
      reportTimer = millis();
    }

    // 2. Listen for Server Commands
    uint8_t target_state;
    if (comms.receiveCommand(target_state)) {
      Serial.printf("Command received: 0x%02x\n", target_state);
      
      // The Rust server sends 0x01 (Heat), 0x02 (Cool), or 0x00 (Off)
      switch (target_state) {
        case 0x01: // Heat On
          digitalWrite(hvacCoolRelay, HIGH); // Enforce mutually exclusive safety
          digitalWrite(hvacHeatRelay, LOW);
          break;
        case 0x02: // Cool On
          digitalWrite(hvacHeatRelay, HIGH); // Enforce mutually exclusive safety
          digitalWrite(hvacCoolRelay, LOW);
          break;
        case 0x00: // Both Off
          digitalWrite(hvacHeatRelay, HIGH);
          digitalWrite(hvacCoolRelay, HIGH);
          break;
        default:
          digitalWrite(hvacHeatRelay, HIGH);
          digitalWrite(hvacCoolRelay, HIGH);
          break;
      }
    }

    delay(10);
  }
}
