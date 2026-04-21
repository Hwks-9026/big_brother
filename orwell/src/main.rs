use std::collections::HashMap;
use std::net::{SocketAddr, UdpSocket};

// --- Data Structures ---

#[derive(Debug)]
pub struct WindowData {
    pub actuator_state: u8,
    pub outside_temp: u16,
    pub inside_temp: u16,
    pub humidity: u16,
}

impl WindowData {
    pub fn decode(payload: u32) -> Self {
        WindowData {
            actuator_state: ((payload >> 30) & 0x03) as u8,
            outside_temp: ((payload >> 20) & 0x3FF) as u16,
            inside_temp: ((payload >> 10) & 0x3FF) as u16,
            humidity: (payload & 0x3FF) as u16,
        }
    }
}

#[derive(Debug)]
pub struct AtticData {
    pub fan_state: u8,
    pub attic_temp: u16,
}

impl AtticData {
    pub fn decode(payload: u32) -> Self {
        AtticData {
            fan_state: ((payload >> 10) & 0x03) as u8,
            attic_temp: (payload & 0x3FF) as u16,
        }
    }
}

// --- State Management ---

struct DeviceRegistry {
    mac_to_id: HashMap<[u8; 6], u8>,
    id_to_addr: HashMap<u8, SocketAddr>,
    next_id: u8,
}

impl DeviceRegistry {
    fn new() -> Self {
        Self {
            mac_to_id: HashMap::new(),
            id_to_addr: HashMap::new(),
            next_id: 1, // zero means unassigned
        }
    }

    fn handle_handshake(&mut self, mac: [u8; 6], addr: SocketAddr) -> u8 {
        let id = *self.mac_to_id.entry(mac).or_insert_with(|| {
            let new_id = self.next_id;
            if self.next_id == 255 {
                self.next_id = 1;
            } else {
                self.next_id += 1;
            }
            new_id
        });
        
        self.id_to_addr.insert(id, addr);
        id
    }
}

// --- Main Server Loop ---

fn main() -> std::io::Result<()> {
    // TODO: Make this keep looking until there's a good socket or smth.
    let socket = UdpSocket::bind("0.0.0.0:8080")?;

    let mut registry = DeviceRegistry::new();
    
    println!("Server listening on port 8080...");
    
    let mut buf = [0u8; 8];

    loop {
        match socket.recv_from(&mut buf) {
            Ok((amt, src)) => {
                if amt == 0 { continue; }
                let msg_type = buf[0];

                // 1. Handle Handshake Requests
                if msg_type == 0x00 {
                    if amt != 8 {
                        eprintln!("Invalid handshake length from {}. Expected 8, got {}", src, amt);
                        continue;
                    }

                    let device_type = buf[1];
                    let mut mac = [0u8; 6];
                    mac.copy_from_slice(&buf[2..8]); // Extract the 6 MAC bytes

                    let assigned_id = registry.handle_handshake(mac, src);
                    
                    // Format MAC address for a clean console print
                    let mac_str = format!("{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}", 
                        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                        
                    println!("Handshake from MAC {}. Assigned Unit ID: {}, Device Type: {:#04x}", 
                             mac_str, assigned_id, device_type);

                    // Send ACK back (2 Bytes: Type + ID)
                    let ack_packet: [u8; 2] = [0x00, assigned_id];
                    socket.send_to(&ack_packet, src)?;
                    continue;
                }

                // 2. Handle Normal Data Packets
                if amt != 6 {
                    eprintln!("Invalid data length from {}. Expected 6, got {}", src, amt);
                    continue;
                }

                let unit_id = buf[1];
                let payload_bytes: [u8; 4] = [buf[2], buf[3], buf[4], buf[5]];
                let payload = u32::from_be_bytes(payload_bytes);

                // Update the IP address map in case the IP changed quietly
                registry.id_to_addr.insert(unit_id, src);

                match msg_type {
                    // Window Data
                    0x01 => { 
                        let data = WindowData::decode(payload);
                        println!("Unit {} (Window) via {}: {:?}", unit_id, src, data);
                        
                        // Example command
                        let command: [u8; 2] = [0x01, 0x03];
                        socket.send_to(&command, src)?;
                    }
                    // Attic Data
                    0x02 => { 
                        let data = AtticData::decode(payload);
                        println!("Unit {} (Attic) via {}: {:?}", unit_id, src, data);
                        
                        // Example Command
                        let command: [u8; 2] = [0x02, 0x02];
                        socket.send_to(&command, src)?;
                    }
                    _ => {
                        eprintln!("Unknown message type {:#04x} from {}", msg_type, src);
                    }
                }
            }
            Err(e) => eprintln!("Failed to receive data: {}", e),
        }
    }
}
