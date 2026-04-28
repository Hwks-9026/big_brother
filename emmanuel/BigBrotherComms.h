#ifndef BIG_BROTHER_COMMS_H
#define BIG_BROTHER_COMMS_H

#include <WiFi.h>
#include <WiFiUdp.h>

// Device Type Definitions
enum BBDeviceType {
    DEVICE_WINDOW     = 0x01,
    DEVICE_ATTIC      = 0x02,
    DEVICE_THERMOSTAT = 0x03
};

class BigBrotherComms {
private:
    const char* _ssid;
    const char* _password;
    IPAddress _server_ip;
    uint16_t _server_port;
    uint8_t _device_type;
    
    WiFiUDP _udp;
    uint8_t _unit_id;
    uint8_t _mac_addr[6];

    void sendPayload(uint32_t payload) {
        uint8_t packet[6];
        packet[0] = _device_type; 
        packet[1] = _unit_id;
        packet[2] = (payload >> 24) & 0xFF;
        packet[3] = (payload >> 16) & 0xFF;
        packet[4] = (payload >> 8)  & 0xFF;
        packet[5] = payload & 0xFF;

        _udp.beginPacket(_server_ip, _server_port);
        _udp.write(packet, 6);
        _udp.endPacket();
        Serial.println("Data payload sent.");
    }

public:
    // Constructor
    BigBrotherComms(const char* ssid, const char* password, IPAddress server_ip, uint16_t server_port, uint8_t device_type)
        : _ssid(ssid), _password(password), _server_ip(server_ip), _server_port(server_port), _device_type(device_type), _unit_id(0x00) {}

    // --- Initialization & Setup ---

    void begin() {
        WiFi.begin(_ssid, _password);
        Serial.print("Connecting to Wi-Fi");
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
        }
        Serial.println("\nConnected to Wi-Fi!");

        WiFi.macAddress(_mac_addr);
        Serial.printf("My MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n", 
                      _mac_addr[0], _mac_addr[1], _mac_addr[2], 
                      _mac_addr[3], _mac_addr[4], _mac_addr[5]);

        _udp.begin(_server_port);
    }

    bool isAssigned() {
        return _unit_id != 0x00;
    }

    // --- Handshake ---

    bool handleHandshake() {
        uint8_t packet[8];
        packet[0] = 0x00;           // Message Type: Handshake
        packet[1] = _device_type;   // Device Type (e.g., 0x01, 0x02, 0x03)
        memcpy(&packet[2], _mac_addr, 6);

        _udp.beginPacket(_server_ip, _server_port);
        _udp.write(packet, 8);
        _udp.endPacket();
        Serial.println("Sent handshake request...");

        // Listen for ACK (Poll for up to 500ms)
        unsigned long start_time = millis();
        while (millis() - start_time < 500) {
            if (_udp.parsePacket() >= 2) {
                uint8_t incomingBuffer[2];
                _udp.read(incomingBuffer, 2);
                
                if (incomingBuffer[0] == 0x00 && incomingBuffer[1] != 0x00) {
                    _unit_id = incomingBuffer[1];
                    Serial.printf("Handshake successful! Assigned Unit ID: %d\n", _unit_id);
                    return true;
                }
            }
            delay(10);
        }
        return false;
    }

    // --- Device-Specific Data Senders ---

    void sendWindowData(uint8_t actuator_state, float outside_temp, float inside_temp, uint16_t humidity) {
        if (_device_type != DEVICE_WINDOW) Serial.println("WARN: Sending Window data from non-Window device");

        uint32_t act   = actuator_state & 0x03;               // 2 bits
        uint32_t out_t = ((uint16_t)outside_temp) & 0x3FF;    // 10 bits
        uint32_t in_t  = ((uint16_t)inside_temp) & 0x3FF;     // 10 bits
        uint32_t hum   = humidity & 0x3FF;                    // 10 bits

        uint32_t payload = (act << 30) | (out_t << 20) | (in_t << 10) | hum;
        sendPayload(payload);
    }

    void sendAtticData(uint8_t fan_state, float attic_temp) {
        if (_device_type != DEVICE_ATTIC) Serial.println("WARN: Sending Attic data from non-Attic device");

        uint32_t fan  = fan_state & 0x03;               // 2 bits
        uint32_t temp = ((uint16_t)attic_temp) & 0x3FF; // 10 bits

        uint32_t payload = (fan << 10) | temp;
        sendPayload(payload);
    }

    void sendThermostatData(bool heat_req, bool cool_req) {
        if (_device_type != DEVICE_THERMOSTAT) Serial.println("WARN: Sending Thermostat data from non-Thermostat device");

        uint32_t h = heat_req ? 1 : 0; // 1 bit
        uint32_t c = cool_req ? 1 : 0; // 1 bit

        uint32_t payload = (h << 1) | c;
        sendPayload(payload);
    }

    bool receiveCommand(uint8_t &target_state) {
        if (_udp.parsePacket()) {
            uint8_t incomingBuffer[2]; 
            int len = _udp.read(incomingBuffer, 2);
            if (len == 2 && incomingBuffer[0] == _device_type) { 
                target_state = incomingBuffer[1];
                return true;
            }
        }
        return false;
    }
};

#endif
