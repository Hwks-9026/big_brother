#include <WiFi.h>
#include <WiFiUdp.h>
#include <dht_async.h>

// --- Network Configuration ---
const char* ssid = ""; // TODO: Edit
const char* password = ""//TODO: Edit

// Raspberry Pi Static IP
IPAddress server_ip(0, 0, 0, 0);
const int server_port = 8080;

WiFiUDP udp;

// --- State Variables ---
uint8_t unit_id = 0x00; // 0x00 means unassigned
uint8_t mac_addr[6];    // Array to hold the 6-byte MAC address

// -- Sensors -- 

const int buttonPin = 25;

const int indoorTempPin = 32;
const int outdoorTempPin = 33;
const int windowDir1Pin = 26;
const int windowDir2Pin = 27;

#define DHT_SENSOR_TYPE DHT_TYPE_11
DHT_Async Indoor_Temp(indoorTempPin, DHT_SENSOR_TYPE);
DHT_Async Outdoor_Temp(outdoorTempPin, DHT_SENSOR_TYPE);

float indoorTempature = 0;
float outdoorTempature = 0;
float indoorHumidity = 0;
float outdoorHumidity = 0;

bool windowState = 0;
bool windowMoving = 0;
bool currState = 0;
bool prevState = 0;

unsigned long int tempTimer = 0;
unsigned long int openingTime = 0;

void setup() {
  Serial.begin(115200);

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi!");

  // Fetch the ESP32's MAC address and store it in an array
  WiFi.macAddress(mac_addr);
  Serial.printf("My MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n", 
                mac_addr[0], mac_addr[1], mac_addr[2], 
                mac_addr[3], mac_addr[4], mac_addr[5]);

  // Start the UDP client
  udp.begin(server_port); 

  // Initialize Sensors
  pinMode(buttonPin, INPUT_PULLUP);

  pinMode(windowDir1Pin, OUTPUT);
  pinMode(windowDir2Pin, OUTPUT);

  digitalWrite(windowDir1Pin, LOW);
  digitalWrite(windowDir2Pin, LOW);
}

void loop() {
  if (unit_id == 0x00) {
    // STATE: Handshaking
    request_handshake();
    listen_for_handshake_ack();
    delay(1000); // Wait 1 second before retrying if no ACK is received
  } else {
    // STATE: Normal Operation
    send_sensor_data();
    listen_for_commands();
    delay(5000); // Normal reporting interval
  }
}

// --- Helper Functions ---

void request_handshake() {
  uint8_t packet[8];
  packet[0] = 0x00; // Message Type: Handshake
  packet[1] = 0x01; // Device Type: Window Sensor
  
  // Copy the 6 MAC address bytes into the packet starting at index 2
  memcpy(&packet[2], mac_addr, 6);

  udp.beginPacket(server_ip, server_port);
  udp.write(packet, 8);
  udp.endPacket();
  
  Serial.println("Sent handshake request...");
}

void listen_for_handshake_ack() {
  unsigned long start_time = millis();
  
  // Poll for up to 500ms
  while (millis() - start_time < 500) {
    int packetSize = udp.parsePacket();
    if (packetSize >= 2) {
      uint8_t incomingBuffer[2];
      udp.read(incomingBuffer, 2);
      
      if (incomingBuffer[0] == 0x00 && incomingBuffer[1] != 0x00) {
        unit_id = incomingBuffer[1];
        Serial.printf("Handshake successful! Assigned Unit ID: %d\n", unit_id);
        return;
      }
    }
    delay(10);
  }
}

void send_sensor_data() {

  if (millis() - tempTimer > 5000) {
    Indoor_Temp.measure(&indoorTempature, &indoorHumidity);
    Outdoor_Temp.measure(&outdoorTempature, &outdoorHumidity);
  }
  uint8_t actuator = (uint8_t)windowMoving;     // e.g., 0=open, 1=closed (2 bits)
  uint16_t out_temp = (uint16_t)outdoorTempature;  // Raw sensor reading (10 bits)
  uint16_t in_temp = (uint16_t)indoorTempature;   // Raw sensor reading (10 bits)
  uint16_t humidity = (uint16_t)(outdoorHumidity);  // Raw sensor reading (10 bits)

  uint32_t payload = ((uint32_t)actuator << 30) | 
                     ((uint32_t)out_temp << 20) | 
                     ((uint32_t)in_temp << 10)  | 
                     (uint32_t)humidity;

  uint8_t packet[6];
  packet[0] = 0x01; // Message Type: Window Data
  packet[1] = unit_id;
  packet[2] = (payload >> 24) & 0xFF; // Big Endian packaging
  packet[3] = (payload >> 16) & 0xFF;
  packet[4] = (payload >> 8) & 0xFF;
  packet[5] = payload & 0xFF;

  udp.beginPacket(server_ip, server_port);
  udp.write(packet, 6);
  udp.endPacket();

  Serial.println("Sensor data sent.");
}

void listen_for_commands() {
  // Briefly wait to see if the server sends a command back
  delay(100);
  
  int packetSize = udp.parsePacket();
  if (packetSize) {
    uint8_t incomingBuffer[2]; // Expecting a 2-byte Command
    int len = udp.read(incomingBuffer, 2);
    
    if (len == 2) {
      uint8_t msg_type = incomingBuffer[0];
      uint8_t target_state = incomingBuffer[1];
      
      if (msg_type == 0x01) { 
        Serial.printf("Received new target state from server: 0x%02x\n", target_state);
        prevState = currState;
        currState = target_state;
        if (currState < prevState && windowMoving == 0) {
          windowMoving = 1;
          if (windowState) {
            digitalWrite(windowDir1Pin, HIGH);
          }
          if (!windowState) {
            digitalWrite(windowDir2Pin, HIGH);
          }
          openingTime = millis();
        }

        if (millis() - openingTime > 30000 && windowMoving == 1) {
          digitalWrite(windowDir1Pin, LOW);
          digitalWrite(windowDir2Pin, LOW);
          windowMoving = 0;
          windowState = !windowState;
        }
      }
    }
  }
}
