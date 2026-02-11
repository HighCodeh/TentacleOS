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
| `free` | Displays available internal and PSRAM memory. | `free` |
| `tasks` | Lists running FreeRTOS tasks and stack usage. | `tasks` |
| `restart` | Reboots the system. | `restart` |
| `ip` | Shows current network interfaces (IP, Mask, GW, MAC). | `ip` |

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

