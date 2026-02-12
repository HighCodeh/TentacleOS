# SPI Bridge - P4 Master

This component manages the high-speed communication link between the **ESP32-P4 (Main OS)** and the **ESP32-C5 (Radio Co-processor)**.

## Architecture
The P4 acts as the **SPI Master**. It is responsible for:
1. Generating the SCLK and managing the CS line.
2. Initiating all command transfers.
3. Handling the **IRQ (Handshake)** signal from the C5 to know when response data is ready.
4. Managing the C5 lifecycle (Reset, Boot mode, and Firmware Updates via UART).

## Protocol Specification
Every packet follows a 4-byte fixed header:
- `Sync (0xAA)`: Packet synchronization.
- `Type`: `0x01` (Command), `0x02` (Response), `0x03` (Stream).
- `ID`: Function identifier (defined in `spi_protocol.h`).
- `Length`: Size of the following payload (0-255 bytes).

## Generic Data Pipe
To keep the bridge simple, we use a "Dumb Pipe" approach for large data sets (like Scan results):
1. **Pull Count**: Call `SPI_ID_SYSTEM_DATA` with index `0xFFFF`.
2. **Pull Item**: Call `SPI_ID_SYSTEM_DATA` with index `0 to N`.
3. **Real-time Stats**: Call `SPI_ID_SYSTEM_DATA` with index `0xEEEE` to get a `sniffer_stats_t` structure.

## Adding a New Command
To add a new feature (e.g., "GPS Get Location"):

1. **Protocol**: Add `SPI_ID_GPS_GET` to `spi_protocol.h`.
2. **C5 Dispatcher**:
   - Open `wifi_dispatcher.c` (or a new `gps_dispatcher.c`).
   - Add the case for `SPI_ID_GPS_GET`.
   - Call the actual hardware driver.
   - If it returns a list, call `spi_bridge_provide_results(pointer, count, size)`.
3. **P4 Wrapper**:
   - Create a wrapper in `Applications` or `Service`.
   - Use `spi_bridge_send_command(SPI_ID_GPS_GET, ...)` to trigger the action.
   - Use the generic `SPI_ID_SYSTEM_DATA` to pull results if necessary.

## Hardware Hookup
| Signal | P4 Pin | C5 Pin |
|--------|--------|--------|
| SCLK   | 20     | 6      |
| MOSI   | 21     | 7      |
| MISO   | 22     | 2      |
| CS     | 23     | 10     |
| IRQ    | 2      | 3      |
| RESET  | 48     | EN     |
| BOOT   | 33     | IO0    |
| UART TX| 46     | RX     |
| UART RX| 47     | TX     |
