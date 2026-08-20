# Console Service Component

The Console Service provides an interactive command-line interface (CLI) for the TentacleOS Highboy. It allows users to manage files, configure system settings, and execute Wi-Fi attacks directly via USB Serial or UART.

It is built on top of the ESP-IDF `esp_console` component and uses `linenoise` for line editing and `argtable3` for argument parsing.

## Accessing the Console

Connect the Highboy to a computer via USB. Use a serial terminal program (e.g., Putty, Screen, minicom) with the following settings:
- **Baud Rate:** 115200 (default)
- **Data Bits:** 8
- **Parity:** None
- **Stop Bits:** 1

The prompt `highboy>` indicates the system is ready.

## Available Commands

### System Commands

| Command | Description | Usage |
| :--- | :--- | :--- |
| `help` | Lists all available commands. | `help [command]` |
| `free` | Displays free internal and PSRAM (SPIRAM) memory, current and minimum. | `free` |
| `tasks` | Lists running FreeRTOS tasks (state, priority, stack high-water, num). | `tasks` |
| `stack` | Stack budget report: allocated vs high-water free per task, with reclaim/low flags. | `stack` |
| `date` | Shows the UTC wall-clock (with sync state and source), or sets it. | `date [<YYYY-MM-DD> <HH:MM:SS>]` |
| `ip` | Shows current network interfaces (IP, Mask, GW, MAC) for STA and AP. | `ip` |
| `restart` | Reboots the system. | `restart` |
| `firstboot` | Clears the first-boot onboarding + screen-tips flags so they run on the next reset. | `firstboot` |
| `capprep` | Skips onboarding (wizard + tips) and restarts clean, for screen capture. | `capprep` |

### File System Commands

| Command | Description | Usage |
| :--- | :--- | :--- |
| `ls` | Lists directory contents. | `ls [-j] [path]`<br>`-j`: Output as JSON |
| `cd` | Changes current working directory. | `cd <path>` |
| `pwd` | Prints current working directory. | `pwd` |
| `cat` | Prints file content to console. | `cat <file>` |

### Wi-Fi Commands (`wifi`)

The `wifi` command is a wrapper for all wireless functions.

| Subcommand | Description | Arguments | Example |
| :--- | :--- | :--- | :--- |
| `scan` | Scans for Wi-Fi networks. | None | `wifi scan` |
| `connect` | Connects to an Access Point. | `-s <ssid>`: Target SSID<br>`-p <pass>`: Password (optional) | `wifi connect -s "MyWifi" -p "1234"` |
| `ap` | Configures the Highboy Hotspot. | `-s <ssid>`: New SSID<br>`-p <pass>`: New Password | `wifi ap -s "FreeWiFi"` |
| `config` | Advanced Wi-Fi settings. | `-e <0/1>`: Enable/Disable<br>`-i <ip>`: Set Static IP<br>`-m <max>`: Max clients | `wifi config -e 1 -m 8` |
| `spam` | Starts Beacon Spam attack. | `-r`: Random SSIDs<br>`-l`: Use `beacon_list.json`<br>`-s`: Stop attack | `wifi spam -r` |
| `deauth` | Starts Deauthentication attack. | `-t <mac>`: Target BSSID<br>`-c <ch>`: Channel<br>`-s`: Stop attack | `wifi deauth -t AA:BB:CC... -c 6` |
| `sniff` | Starts Packet Sniffer. | `-t <type>`: beacon, probe, pwn, raw<br>`-c <ch>`: Channel (0=Hop)<br>`-f <file>`: Save to SD<br>`-v`: Verbose (print)<br>`-s`: Stop | `wifi sniff -t beacon -v` |
| `probe` | Monitors Probe Requests. | `start` / `-s` (Stop) | `wifi probe start` |
| `clients` | Scans connected clients (sniffer). | `start` / `-s` (Stop) | `wifi clients start` |
| `target` | Monitors specific target activity. | `-t <mac>`: Target MAC<br>`-c <ch>`: Channel<br>`-s`: Stop | `wifi target -t AA:BB... -c 6` |
| `evil` | Starts Evil Twin (Captive Portal). | `-s <ssid>`: Fake AP Name<br>`-s`: Stop (use --stop flag) | `wifi evil -s "Google Free"` |
| `portscan`| Scans TCP/UDP ports on target. | `-i <ip>`: Target IP<br>`-min`: Start Port<br>`-max`: End Port | `wifi portscan -i 192.168.1.1` |
| `status` | Shows active attacks and state. | None | `wifi status` |

### BadUSB Commands (`badusb`)

HID injection via DuckyScript. Hint: `badusb <run|type|layout|stop|status> ...`.

| Subcommand | Description | Arguments | Example |
| :--- | :--- | :--- | :--- |
| `run` | Runs a DuckyScript. | `-a <name>`: internal asset (under `bad_usb_scripts/`)<br>`-f <path>`: script from SD card | `badusb run -a rickroll.txt` |
| `type` | Types a literal string over HID. | `<text...>` | `badusb type hello world` |
| `layout` | Sets the keyboard layout. | `<us\|abnt2>` | `badusb layout abnt2` |
| `stop` | Aborts the running script (stops at the next line). | None | `badusb stop` |
| `status` | Shows whether the USB host has mounted the device. | None | `badusb status` |

### Host Link Commands (`hostlink`)

Companion (BLE) host-link pairing and status.

| Subcommand | Description | Arguments | Example |
| :--- | :--- | :--- | :--- |
| `psk` | Shows the pairing PSK to provision the companion app. | None | `hostlink psk` |
| `regen` | Generates a new PSK (invalidates existing pairings), then prints it. | None | `hostlink regen` |
| `ble` | Turns companion BLE advertising on/off. | `<on\|off>` | `hostlink ble on` |
| `status` | Shows the companion session (authenticated) and BLE connection state. | None | `hostlink status` |

### C5 Co-processor Commands (`c5`)

Manages the C5 radio co-processor over the SPI bridge (and UART for recovery).

| Subcommand | Description | Example |
| :--- | :--- | :--- |
| `ota <spi\|uart>` | Pushes the C5 image; control on SPI, image bytes over SPI (default) or UART. | `c5 ota spi` |
| `ping` | Pings the running C5 over the SPI bridge. | `c5 ping` |
| `info` | Reads the C5 chip info over the SPI bridge. | `c5 info` |
| `sync` | Re-probes and reconnects the bridge (e.g. after a C5 reboot). | `c5 sync` |
| `download` | Asks the running C5 to enter ROM download mode (over SPI). | `c5 download` |
| `rom` | Serial-flashes a C5 already in download mode (recovery). | `c5 rom` |
| `passthrough` | Bridges host `esptool` to the C5 (never returns; reboot/BACK exits). | `c5 passthrough` |
| `release` | Tri-states the C5 UART lines for an external programmer. | `c5 release` |

### Infrared Commands (`ir`)

RMT-backed IR receive/transmit. The `rx`/`send` channels are brought up on demand and released after each operation; `flash` stays on until turned off.

| Subcommand | Description | Arguments | Example |
| :--- | :--- | :--- | :--- |
| `rx` | Waits for one IR frame and decodes it. | `[timeout_ms]` (default 5000) | `ir rx 10000` |
| `send` | Transmits a code (address/command accept `0x` hex). | `<proto> <addr> <cmd> [repeat]` | `ir send NEC 0x00 0x45` |
| `flash` | Drives the IR emitter at full power (torch) / off. | `<on\|off>` | `ir flash on` |

### Hardware / Diagnostics

| Command | Description | Usage |
| :--- | :--- | :--- |
| `battery` | Shows battery/charger (BQ25896) status: SoC (raw + smoothed), voltage, charge state, VBUS source and limits, faults. | `battery` |
| `i2cscan` | Scans the I2C bus (`0x08`-`0x77`) and lists responding addresses. | `i2cscan` |

### UI / Screen Commands

Debug/capture helpers registered as top-level commands (from the screen command group). Used for automated screenshotting and UI navigation.

| Command | Description | Usage |
| :--- | :--- | :--- |
| `goto` | Switches the UI to a screen by enum id. | `goto <id>` (1..`SCREEN_COUNT`-1) |
| `key` | Injects a simulated button press. | `key <idx> [-t <ms>]`<br>idx: 0=UP 1=DOWN 2=LEFT 3=RIGHT 4=OK 5=BACK; hold default 120 ms |
| `screenshot` | Captures the active screen as base64 RGB565 strips. | `screenshot [id]` (optionally switch to `id` first) |

## Developing New Commands

To add a new command to the console, follow these steps:

1.  **Create a source file:** Create `commands/cmd_mycommand.c`.
2.  **Define Arguments:** Use `argtable3` structs to define parameters.
3.  **Implement Handler:** Create a static function `int cmd_mycommand(int argc, char **argv)`.
4.  **Register:** Create a public registration function and call `esp_console_cmd_register`.
5.  **Hook:** Call your registration function in `console_service.c`.

### Example Template

```c
#include "console_service.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"

static struct {
    struct arg_str *message;
    struct arg_end *end;
} echo_args;

static int cmd_echo(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&echo_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, echo_args.end, "echo");
        return 1;
    }
    printf("Echo: %s\n", echo_args.message->sval[0]);
    return 0;
}

void register_echo_command(void) {
    echo_args.message = arg_str1(NULL, NULL, "<msg>", "Message to print");
    echo_args.end = arg_end(1);

    const esp_console_cmd_t echo_cmd = {
        .command = "echo",
        .help = "Print a message",
        .func = &cmd_echo,
        .argtable = &echo_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&echo_cmd));
}
```

