#include "BigBrotherComms.h"
#include "DHT_Async.h"

// --- Network Configuration ---
const char* ssid = "";
const char* password = "";
IPAddress server_ip(0, 0, 0, 0); 
const int server_port = 8080;

// Initialize as DEVICE_ATTIC (0x02)
BigBrotherComms comms(ssid, password, server_ip, server_port, DEVICE_ATTIC);

// --- Hardware & Sensors --- 
const int atticTempPin = 2;
const int fanRelayPin = 8;

#define DHT_SENSOR_TYPE DHT_TYPE_11
DHT_Async Attic_Temp(atticTempPin, DHT_SENSOR_TYPE);

// --- State Variables ---
float atticTemperature = 0;
float dummyHumidity = 0; // Required for DHT lib but unused in protocol
uint8_t currentFanState = 0x00; 

unsigned long reportTimer = 0;

void setup() {
  Serial.begin(115200);

  comms.begin(); 

  pinMode(fanRelayPin, OUTPUT);
  digitalWrite(fanRelayPin, HIGH); // Default to off
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
    // 1. Read Sensors & Send Data (Every 5 seconds)
    if (millis() - reportTimer > 5000) {
      Attic_Temp.measure(&atticTemperature, &dummyHumidity);
      comms.sendAtticData(currentFanState, atticTemperature);
      reportTimer = millis();
    }

    // 2. Listen for Commands
    uint8_t target_state;
    if (comms.receiveCommand(target_state)) {
      
      // The Rust server sends 0x02 for ON and 0x00 for OFF
      if (target_state == 0x02) {
        digitalWrite(fanRelayPin, LOW);
        currentFanState = 0x02;
      } else if (target_state == 0x00) {
        digitalWrite(fanRelayPin, HIGH);
        currentFanState = 0x00;
      }
    }

    delay(10);
  }
}
