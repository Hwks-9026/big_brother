#include <WiFi.h>
#include <WiFiUdp.h>

// --- Network Configuration ---
const char* ssid = "Pixel 8 Pro"; // TODO: Edit
const char* password = "usuck122"; // TODO: Edit

// Raspberry Pi Static IP
const char* server_ip = "10.186.208.5"; // TODO: Edit
const int server_port = 8080;

WiFiUDP udp;

// --- State Variables ---
uint8_t unit_id = 0x00; // 0x00 means unassigned
uint8_t mac_addr[6];    // Array to hold the 6-byte MAC address

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

  // Fetch the ESP32's MAC address and store it in the array
  WiFi.macAddress(mac_addr);
  Serial.printf("My MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n", 
                mac_addr[0], mac_addr[1], mac_addr[2], 
                mac_addr[3], mac_addr[4], mac_addr[5]);

  // Start the UDP client
  udp.begin(server_port); 
}

void loop() {
  if (unit_id == 0x00) {
    // STATE: Handshaking
    request_handshake();
    liandshake_ack();
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
  // Briefly wait to see if a response comes in
  delay(100); 
  
  int packetSize = udp.parsePacket();
  if (packetSize) {
    uint8_t incomingBuffer[2]; // Expecting a 2-byte ACK
    int len = udp.read(incomingBuffer, 2);
    
    if (len == 2) {
      uint8_t msg_type = incomingBuffer[0];
      uint8_t assigned_id = incomingBuffer[1];
      
      if (msg_type == 0x00 && assigned_id != 0x00) {
        unit_id = assigned_id;
        Serial.printf("Handshake successful! Assigned Unit ID: %d\n", unit_id);
      }
    }
  }
}

void send_sensor_data() {
  uint8_t actuator = 1;     // e.g., 0=open, 1=closed (2 bits)
  uint16_t out_temp = 512;  // Raw sensor reading (10 bits)
  uint16_t in_temp = 600;   // Raw sensor reading (10 bits)
  uint16_t humidity = 400;  // Raw sensor reading (10 bits)

  uint32_t payload = (actuator << 30) | (out_temp << 20) | (in_temp << 10) | humidity;

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
      }
    }
  }
}
