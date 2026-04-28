use clap::Parser;
use std::collections::HashMap;
use std::net::{SocketAddr, UdpSocket};

// --- CLI Arguments ---

#[derive(Parser, Debug)]
#[command(author, version, about = "Big Brother: HVAC & Ventilation Controller")]
struct Args {
    #[arg(short, long, default_value_t = 71.0)]
    target_temp: f32,

    // Comfort range: System does nothing if within target +/- this value
    #[arg(short, long, default_value_t = 1.0)]
    comfort_range: f32,

    // Fan-only range: Will attempt to use only outside air if temp is within target +/- this value (e.g., 5.0)
    #[arg(short, long, default_value_t = 5.0)]
    fan_only_range: f32,
}

// --- Data Structures ---

#[derive(Debug, Clone, Copy)]
pub struct WindowData {
    pub actuator_state: u8,
    pub outside_temp: f32, // Converted to f32 for logic
    pub inside_temp: f32,
    pub humidity: u16,
}

impl WindowData {
    pub fn decode(payload: u32) -> Self {
        dbg!(WindowData {
            actuator_state: ((payload >> 30) & 0x03) as u8,
            outside_temp: (((payload >> 20) & 0x3FF) as f32) * 1.8 + 32.0, // convesrion to F
            inside_temp: (((payload >> 10) & 0x3FF) as f32) * 1.8 + 32.0,
            humidity: (payload & 0x3FF) as u16,
        })
    }
}

#[derive(Debug, Clone, Copy)]
pub struct AtticData {
    pub fan_state: u8,
    pub attic_temp: f32,
}

impl AtticData {
    pub fn decode(payload: u32) -> Self {
        dbg!(AtticData {
            fan_state: ((payload >> 10) & 0x03) as u8,
            attic_temp: ((payload & 0x3FF) as f32) * 1.8 + 32.0,
        })
    }
}

#[derive(Debug, Clone, Copy)]
pub struct ThermostatData {
    pub heat_req: bool,
    pub cool_req: bool,
}

impl ThermostatData {
    pub fn decode(payload: u32) -> Self {
        dbg!(ThermostatData {
            heat_req: ((payload >> 1) & 0x01) == 1,
            cool_req: (payload & 0x01) == 1,
        })
    }
}

// --- State Management ---

#[derive(Debug, Default, Clone)]
struct HomeState {
    window: Option<WindowData>,
    attic: Option<AtticData>,
    thermostat: Option<ThermostatData>,
}

#[derive(Debug, PartialEq)]
struct SystemCommand {
    window_open: bool,
    attic_fan_on: bool,
    hvac_state: u8, // 0x00= Both Off, 0x01=Heat, 0x02=Cool
}

struct DeviceRegistry {
    mac_to_id: HashMap<[u8; 6], u8>,
    id_to_addr: HashMap<u8, SocketAddr>,
    type_to_ids: HashMap<u8, Vec<u8>>, // Maps device type to unit IDs
    next_id: u8,
}

impl DeviceRegistry {
    fn new() -> Self {
        Self {
            mac_to_id: HashMap::new(),
            id_to_addr: HashMap::new(),
            type_to_ids: HashMap::new(),
            next_id: 1,
        }
    }

    fn handle_handshake(&mut self, mac: [u8; 6], addr: SocketAddr) -> u8 {
        let id = *self.mac_to_id.entry(mac).or_insert_with(|| {
            let new_id = self.next_id;
            self.next_id = if self.next_id == 255 { 1 } else { self.next_id + 1 };
            new_id
        });
        self.id_to_addr.insert(id, addr);
        id
    }

    fn register_device_type(&mut self, msg_type: u8, unit_id: u8) {
        let ids = self.type_to_ids.entry(msg_type).or_insert_with(Vec::new);
        if !ids.contains(&unit_id) {
            ids.push(unit_id);
        }
    }
}

// --- Control Engine ---

struct ControlEngine {
    target: f32,
    comfort_range: f32,
    fan_only_range: f32,
}

impl ControlEngine {
    fn new(args: &Args) -> Self {
        Self {
            target: args.target_temp,
            comfort_range: args.comfort_range,
            fan_only_range: args.fan_only_range,
        }
    }

    /// Evaluates the whole home state and returns the target states for all devices
    fn evaluate(&self, state: &HomeState) -> SystemCommand {
        // Default to everything off
        let mut cmd = SystemCommand {
            window_open: false,
            attic_fan_on: false,
            hvac_state: 0x00,
        };

        if let Some(window) = &state.window {
            let t_in = window.inside_temp;
            let t_out = window.outside_temp;
            let do_cooling = if let Some(thermostat) = &state.thermostat {
                thermostat.cool_req
            } else {t_in > self.target + self.comfort_range};
            if  do_cooling {
                let inside_fan_range = t_in <= self.target + self.fan_only_range;
                let outside_is_cooler = t_out < t_in;

                if inside_fan_range && outside_is_cooler {
                    // We can use outside air for free cooling
                    cmd.window_open = true;
                    cmd.attic_fan_on = true;
                    cmd.hvac_state = 0x00; // AC off
                } else if outside_is_cooler {
                    // Too hot for just the fan, or outside is too warm. Use AC.
                    cmd.window_open = true;
                    cmd.attic_fan_on = true;
                    cmd.hvac_state = 0x02; // AC On
                }
                else {
                    cmd.window_open = false;
                    cmd.attic_fan_on = false;
                    cmd.hvac_state = 0x02; // AC On

                }
            }

            else if t_in < self.target - self.comfort_range {
                let inside_fan_range = t_in >= self.target - self.fan_only_range;
                let outside_is_warmer = t_out > t_in;

                if inside_fan_range && outside_is_warmer {
                    // We can use outside air for free heating (unlikely but technically possible)
                    cmd.window_open = true;
                    cmd.attic_fan_on = true;
                    cmd.hvac_state = 0x00;
                } else {
                    // Use Heater
                    cmd.window_open = false;
                    cmd.attic_fan_on = false;
                    cmd.hvac_state = 0x01; // Heat On
                }
            }
            else {
                cmd.window_open = false;
                cmd.attic_fan_on = false;
                cmd.hvac_state = 0x00;
            }
        }

        cmd
    }
}

// --- Main Server Loop ---

fn main() -> std::io::Result<()> {
    let args = Args::parse();
    println!("Starting server with settings:");
    println!("Target: {}°", args.target_temp);
    println!("Comfort Range: ±{}°", args.comfort_range);
    println!("Fan-Only Range: ±{}°", args.fan_only_range);

    let socket = UdpSocket::bind("0.0.0.0:8080")?;
    let mut registry = DeviceRegistry::new();
    let mut home_state = HomeState::default();
    let control_engine = ControlEngine::new(&args);
    
    let mut buf = [0u8; 8];

    loop {
        match socket.recv_from(&mut buf) {
            Ok((amt, src)) => {
                if amt == 0 { continue; }
                let msg_type = buf[0];

                // handshake handling
                if msg_type == 0x00 {
                    if amt != 8 { continue; }
                    let mut mac = [0u8; 6];
                    mac.copy_from_slice(&buf[2..8]);
                    let assigned_id = registry.handle_handshake(mac, src);
                    
                    let ack_packet: [u8; 2] = [0x00, assigned_id];
                    socket.send_to(&ack_packet, src)?;
                    continue;
                }

                if amt != 6 { continue; }
                let unit_id = buf[1];
                let payload = u32::from_be_bytes([buf[2], buf[3], buf[4], buf[5]]);
                
                // update network mapping
                registry.id_to_addr.insert(unit_id, src);
                registry.register_device_type(msg_type, unit_id);

                // decode incoming data and update global state
                match msg_type {
                    0x01 => { home_state.window = Some(WindowData::decode(payload)); }
                    0x02 => { home_state.attic = Some(AtticData::decode(payload)); }
                    0x03 => { home_state.thermostat = Some(ThermostatData::decode(payload)); }
                    _ => { continue; } // Ignore unknown message types
                }
    
                // evaluate the overall system state
                let desired_cmd = control_engine.evaluate(&home_state);
                dbg!(&desired_cmd);
                // respond to the device that just updated the server
                match msg_type {
                    0x01 => {
                        let w_state = if desired_cmd.window_open { 0x01 } else { 0x00 };
                        let pkt: [u8; 2] = [0x01, w_state];
                        let _ = socket.send_to(&pkt, src);
                    }
                    0x02 => {
                        let f_state = if desired_cmd.attic_fan_on { 0x02 } else { 0x00 };
                        let pkt: [u8; 2] = [0x02, f_state];
                        let _ = socket.send_to(&pkt, src);
                    }
                    0x03 => {
                        let pkt: [u8; 2] = [0x03, desired_cmd.hvac_state];
                        let _ = socket.send_to(&pkt, src);
                    }
                    _ => {}
                }
            }
            Err(e) => eprintln!("Failed to receive data: {}", e),
        }
    }
}
