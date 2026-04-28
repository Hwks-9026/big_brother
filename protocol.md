# Big Brother Protocol
- UDP over standard Wi-Fi (IPv4)
- Big Endian (Network Byte Order) for all multi-byte sequences.

| Msg Type | Packet Name | Direction | Length | Type | Byte 1 | Bytes 2+ (Payload & Bit Structures) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `0x00` | **Handshake Request** | Node   -> Server | 8 Bytes | `0x00` | Requested Device Type (`0x01`=Window, `0x02`=Attic) | MAC Address (6 Bytes) |
| `0x00` | **Handshake ACK** | Server -> Node | 2 Bytes | `0x00` | Assigned Unit ID (`0x01`-`0xFF`) | *None* |
| `0x01` | **Window Sensor Data** | Node   -> Server | 6 Bytes | `0x01` | Assigned Unit ID | Actuator(2b) OutTemp(10b) InTemp(10b) Hum(10b) |
| `0x01` | **Window Command** | Server -> Node | 2 Bytes | `0x01` | Target Actuator State (`0x00`-`0x01`) | *None* |
| `0x02` | **Attic Sensor Data** | Node   -> Server | 6 Bytes | `0x02` | Assigned Unit ID | Reserved(20b) Fan(2b) AtticTemp(10b) |
| `0x02` | **Attic Command** | Server -> Node | 2 Bytes | `0x02` | Target Fan State (`0x00`-`0x02`) | *None* |
| `0x03` |**Thermostat Data**| Node   -> Server | 6 Bytes | `0x03` | Heat requirement and cool requirement (1 bit each but keep packet the same size for consistancy)| *None*|
| `0x03` |**Thermostat Command**| Server -> Node | 2 Bytes | `0x03` | Toggle A/C or Heat (1 bit each but keep packet the same size for consistancy)| *None*|


In the final protocol, the message length will be fixed and substantially longer than 8 bytes (the packet overhead is significant enough to justify this).
Remaining area will be used for significant anti-corruption measures. The system does not need to work fast, but it does need to work correctly, so error
correction beyond what is typical will be used in all instances where transmition over wifi is neccesary.
