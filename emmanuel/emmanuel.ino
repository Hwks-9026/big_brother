#include "BigBrotherComms.h"
#include "DHT_Async.h"

// --- Network Configuration ---
const char* ssid = "";
const char* password = "";
IPAddress server_ip(0, 0, 0, 0); 
const int server_port = 8080;

// Initialize as DEVICE_WINDOW (0x01)
BigBrotherComms comms(ssid, password, server_ip, server_port, DEVICE_WINDOW);

// --- Hardware & Sensors --- 
const int buttonPin = 25;
const int indoorTempPin = 32;
const int outdoorTempPin = 33;
const int windowDir1Pin = 26;
const int windowDir2Pin = 27;

#define DHT_SENSOR_TYPE DHT_TYPE_11
DHT_Async Indoor_Temp(indoorTempPin, DHT_SENSOR_TYPE);
DHT_Async Outdoor_Temp(outdoorTempPin, DHT_SENSOR_TYPE);

// --- State Variables ---
float indoorTemperature = 0;
float outdoorTemperature = 0;
float indoorHumidity = 0;
float outdoorHumidity = 0;

bool windowState = 1;
bool windowMoving = 0;

unsigned long tempTimer = 0;
unsigned long openingTime = 0;

void setup() {
  Serial.begin(115200);

  // Initialize Network
  comms.begin(); 

  // Initialize Hardware
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(windowDir1Pin, OUTPUT);
  pinMode(windowDir2Pin, OUTPUT);

  digitalWrite(windowDir1Pin, LOW);
  digitalWrite(windowDir2Pin, LOW);

  digitalWrite(windowDir2Pin, HIGH);
  delay(20000);
  digitalWrite(windowDir2Pin, LOW);

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
    // 1. Read Sensors (Every 5 seconds)
    if (millis() - tempTimer > 5000) {
      Indoor_Temp.measure(&indoorTemperature, &indoorHumidity);
      Outdoor_Temp.measure(&outdoorTemperature, &outdoorHumidity);
      tempTimer = millis();
      
      // Send windowState or windowMoving depending on how you want the server to see it
      comms.sendWindowData(windowMoving ? 2 : windowState, outdoorTemperature, indoorTemperature, outdoorHumidity);
    }

    uint8_t target_state;
    if (comms.receiveCommand(target_state)) {
      Serial.printf("Command received: 0x%02x\n", target_state);
      
      bool desired_open = (target_state == 0x01);

      if (desired_open != windowState && windowMoving == 0) {
        windowMoving = 1;
        if (desired_open) {
          digitalWrite(windowDir1Pin, HIGH);
        } else {
          digitalWrite(windowDir2Pin, HIGH);
        }
        openingTime = millis();
      }
    }

    if (windowMoving == 1 && (millis() - openingTime > 30000)) {
      digitalWrite(windowDir1Pin, LOW);
      digitalWrite(windowDir2Pin, LOW);
      windowMoving = 0;
      windowState = !windowState;
    }

    delay(10);
  }
}
