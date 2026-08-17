# ESP-NOW Chat Application

The **ESP-NOW Chat Application** is the high-level logic layer that bridges the raw `Service` capabilities with the User Interface (UI). It handles business logic, event notification, and data formatting for the display.

## Overview

This component sits between the **UI Manager** (LVGL) and the **ESP-NOW Service**. It ensures that the UI doesn't need to know about raw bytes, MAC addresses, or packet types, providing a clean API for "sending messages" and "listing users".

## Features

- **Event-Driven UI Updates**: Provides a callback mechanism so the UI only updates when necessary (new message, new device found).
- **System Notifications**: automatically injects system messages (e.g., "Secure Pair with User!") into the chat stream.
- **Simplified API**: Wraps complex service calls into single-line functions for the UI.
- **Data Abstraction**: Converts service-level structs into UI-friendly structs.

## Integration Guide

### 1. Initialization
In your `main.c` or `ui_manager.c`:

```c
#include "espnow_chat.h"

void app_main() {
    // ... WiFi Init ...
    
    // Initialize the Chat App
    espnow_chat_init();
    
    // Register UI Callbacks
    espnow_chat_register_msg_cb(my_ui_message_handler);
    espnow_chat_register_refresh_cb(my_ui_device_list_refresh);
}
```

### 2. Handling Messages in UI
The UI should implement a callback to receive messages:

```c
void my_ui_message_handler(const char *sender_nick, const char *message, bool is_system_msg) {
    if (is_system_msg) {
        // Render in yellow/red
        ui_chat_add_bubble_system(message);
    } else {
        // Render in bubble
        ui_chat_add_bubble(sender_nick, message);
    }
}
```

### 3. Listing Devices
When the user opens the "Scan" tab, the UI calls:

```c
espnow_chat_peer_t peers[10];
int count = espnow_chat_get_peer_list(peers, 10);

for(int i=0; i<count; i++) {
    printf("Found: %s (RSSI: %d)\n", peers[i].nick, peers[i].rssi);
    // Add button to UI list
}
```

## API Reference

### Setup
- `espnow_chat_init()`: Starts the app and underlying service.
- `espnow_chat_deinit()`: Stops everything.
- `espnow_chat_set_nick(char*)`: Updates user nickname.
- `espnow_chat_set_online(bool)`: Toggles "Airplane mode" for Chat.

### Actions
- `espnow_chat_broadcast_discovery()`: Sends "HELLO". Call this when opening the "Search" screen.
- `espnow_chat_send_message(mac, text)`: Sends a message to a specific user.
- `espnow_chat_secure_pair(mac)`: Initiates the key exchange handshake with a user.

### Data
- `espnow_chat_get_peer_list(...)`: Returns the list of discovered devices (online session).
- `espnow_chat_save_peer(mac, name)`: Saves a temporary peer to the permanent address book.

## Logic Flow

1. **Discovery**:
   - User opens Scan Screen -> UI calls `espnow_chat_broadcast_discovery()`.
   - Service sends HELLO.
   - Other devices receive HELLO -> Service auto-adds to list -> App triggers `refresh_cb` -> UI updates list.

2. **Chatting**:
   - User taps a device -> UI enters Chat Screen.
   - User types "Hi" -> UI calls `espnow_chat_send_message()`.
   - Service encrypts & sends.

3. **Secure Pairing**:
   - User taps "Secure Pair" -> UI calls `espnow_chat_secure_pair()`.
   - Service generates Key (if none) -> Sends `KEY_SHARE` packet.
   - Target receives `KEY_SHARE` -> App triggers `msg_cb` ("Secure Pair with X!") -> Service saves key.
   - Future messages are now secure.

