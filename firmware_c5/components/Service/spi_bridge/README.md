# SPI Bridge - C5 Slave

This component transforms the **ESP32-C5** into a high-performance radio co-processor for the ESP32-P4.

## How it Works
The C5 runs a background task (`spi_bridge_task`) that stays in a blocked state waiting for the P4 to send SPI bytes. 

1. **Reception**: When bytes arrive, the task validates the `0xAA` sync byte.
2. **Routing**: It checks the `ID` and routes the payload to the appropriate **Dispatcher** (WiFi or Bluetooth).
3. **Execution**: The Dispatcher executes the radio command (e.g., starts a scan).
4. **Notification**: Once the command is done (or results are ready), the C5 raises the **IRQ (Handshake)** pin.
5. **Response**: The P4 sees the IRQ, sends a dummy SPI clock, and the C5 "pushes" the response packet back.

## Memory Mapping (Zero-Copy Results)
The C5 uses a `current_data_source` pointer system. Instead of copying large scan lists into a bridge buffer, the Dispatcher simply points the bridge to the existing result array in memory:
```c
spi_bridge_provide_results(wifi_records, count, sizeof(wifi_ap_record_t));
```
The bridge then serves these items one by one when the P4 asks for them via the generic `SPI_ID_SYSTEM_DATA` command.

## Key Files
- `spi_bridge.c`: Main task and generic data provider logic.
- `wifi_dispatcher.c`: Logic to translate SPI IDs to WiFi driver calls.
- `bt_dispatcher.c`: Logic to translate SPI IDs to NimBLE/BT calls.
- `spi_slave_driver.c`: Low-level peripheral configuration.

## Command Range
- `0x01 - 0x0F`: System/Bridge management.
- `0x10 - 0x4F`: WiFi operations.
- `0x50 - 0x7F`: Bluetooth operations.
- `0x80 - 0x8F`: LoRa operations.
