<!--
Copyright (c) 2025 HIGH CODE LLC

TentacleOS is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

TentacleOS is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.
-->

# Navigation audit

Scope: static review of registered native routes, screen source files
(including shared builders/wrappers), and BACK/ESC handlers. Pending SubGHz
changes are excluded from this inventory.
This is a code audit, not a claim that every route was exercised on hardware.
The inventory below identifies the implementation and explicit close hook for
all registered routes. Shared builders and custom ELF-app navigation need their
own state contracts; a route ID is not necessarily a single visual page.

## Corrected return paths

| Entry path | BACK behavior after correction |
|---|---|
| Developer → Apps | Returns to Developer, not the coverflow root. |
| Settings → Interface → Theme | Returns to Interface; direct Settings → Theme still returns to Settings. |
| Settings → Audio & Haptics → tool | Returns to Audio & Haptics, retaining that submenu rather than skipping to Settings root. |
| Settings main/Firmware → C5 Control → Console | Console returns to C5 Control; C5 Control returns to the originating Settings view. |
| NFC Read → dictionary/APDU/bank card/write/emulate/protocol tool | Returns to Read; direct NFC menu entry still returns to the NFC menu. Read is reopened, not a suspended snapshot of the previous RF operation. |
| NFC Saved → emulation (Classic/Type V/Texkom) | Returns to Saved instead of the NFC root. |
| LoRa → selected protocol → feature | Returns to the protocol home, preserving protocol selection even before a session connects. |
| Wi-Fi → signal locator → target | Stops polling and returns to the retained AP selection list. |
| Wi-Fi → target clients → target | Stops polling and returns to the retained AP selection list. |
| IR Send → completed-send options | Returns to the signal list rather than skipping to the feature root. |
| Home → shortcut → nested feature | Child returns remain within the feature; the feature root returns to Home. |
| Settings/Audio & Haptics/Interface/Infrared → child → BACK | Reopens the parent with its selected row retained. |
| Theme selector → apply confirmation → BACK | Cancels without writing the theme or restarting; the next BACK returns to the caller. |

Screen deletion now clears an owner slot only if that slot still points to the
object being deleted. Rebuilding a view before deleting its old screen must not
clear the new screen's pointer. The central built-in transition also watches the
outgoing root's DELETE event while its replacement is built. A builder can delete
that root synchronously, and the allocator can reuse its address for a new child;
`lv_obj_is_valid()` alone cannot distinguish those lifetimes. Once the old root
is deleted it is never queued for another asynchronous delete. The temporary
watch is removed before returning, and registration failure leaves the existing
screen intact. Host regressions cover address reuse and ordinary delayed teardown.

## Other route contracts reviewed

- Connections returns to Settings; Connect Wi-Fi/Bluetooth returns to Connections.
- Wi-Fi live scanning uses the first BACK to stop acquisition and expose results;
  results/configuration and per-feature root navigation remain separate steps.
- BLE detection tools return to Detection; advertising execution returns to its
  selector, then the BLE root. HID pairing/mouse transitions share radio ownership.
- IR saved signals retain command → file → protocol → IR root levels. Remote
  controllers return to device selection. IR library navigation is managed by its
  own builder rather than a separate registered route.
- RFID, BadUSB, Files and LoRa chat use internal view state for nested pages;
  their root exits remain Menu. Audio/image/video callers explicitly set `s_return`.
- Games return to Games; Developer diagnostics return to Developer. Native app
  launchers hand control to the separate app-screen lifecycle.
- USB Storage intentionally returns Home when the SD cannot be remounted; its
  normal successful exit returns Files. This error-recovery exception is not a
  duplicate BACK. Formatting/write/update states can consume BACK as cancellation
  or defer exit while a critical write is active.
- Home shortcuts retain a single root return to Home, not a general history
  stack. Internal feature exits keep their declared parent. Opening coverflow
  clears the shortcut origin, so a later feature entered there returns to Menu.

## Lifecycle findings still open

These findings are separate from the corrected return destinations. They prevent
claiming that every peripheral stops immediately on all exits:

1. **BLE Scan and Connect Bluetooth:** their workers can finish a timed scan after
   navigation. Completion callbacks check the screen ID, but not an entry generation;
   re-entering now resumes an in-flight scan rather than clearing its state or
   spawning a duplicate worker. A late completion still belongs to that resumed
   operation; immediate cancellation remains unavailable. Neither
   route has a central close hook. The scanner API currently exposes no cancel call.
2. **Programmatic exits/shutdown:** several Wi-Fi, IR, audio and game routes rely on
   BACK handlers or a later timer's active-screen check instead of a registered stop
   hook. The inventory marks these explicitly; absence of a hook alone is not proof
   of a leak, but is not equivalent to synchronous ownership release either.
Hotspot now has a central close hook and guarded DELETE fallback; opening its
settings no longer starts an AP. Its worker reports failures and honors a stop
requested while startup is in flight.

The MP4 metadata viewer now has a central close hook: it detaches the cover
widget, drops cached decoded images and releases JPEG/PNG buffers before leaving.
The old no-op polling timer is removed. Its guarded DELETE fallback cannot
release resources owned by a newer screen. BLE detection BACK handlers use the
central stop hook once instead of repeating the C5 deinitialization command.

A follow-up lifecycle change must cancel/join operations without blocking the LVGL
thread, reject stale completions on re-entry, and release buffers only after their
widgets/peripherals stop using them. Replacing these with an arbitrary delay or
unconditionally deinitializing a shared radio would introduce a different race.

## Validation

`tools/highboy/tests/test_ui_navigation.py` compiles production router/keypad code
and the Settings/LoRa opening functions with host fakes and sanitizer support.
The November 18 macOS run used UBSan; ASan could not initialize on that host. It covers
short/held BACK, popup gesture ownership, keyboard/legacy keypad exclusion,
Settings and LoRa subview returns, and delayed deletion of an old screen.
The Wi-Fi live scan and format UI host suites also pass. P4 build is checked
separately; host mocks do not validate actual radio teardown or physical timing.

The 18 November UI audit maps 135 registered native routes. Candidate `ui07`
(SHA-256 `a2678b002913dbced52ea4b3373a7833735f3291037819fbb09e82b8b2a51e63`)
passed entry/BACK checks for 119 routes in portrait without reset markers,
truncated snapshots or a non-Home route remaining after the expected BACK steps.
The final `ui08` candidate adds compact footer hints and single-line grid titles.
On that candidate, three acknowledged physical-key simulations 152.5/152.3 ms
apart moved the carousel from Wi-Fi to RFID; an immediate left/OK sequence
opened NFC, then BACK returned to Menu and Home. The last grid item was visible
in both orientations. Six Home/Menu/NFC cycles held internal free RAM at
80,603–80,607 bytes; PSRAM free changed by 260 bytes from first to last sample
(31,983,400 to 31,983,140). This short run is not a global leak-free guarantee.
Portrait rotation, default theme and 100% brightness were restored; NFC emulation
reported idle. Final screenshots confirm compact LED/SD/telemetry footer hints.

The original wider-layout pass covered 116 completed routes before corrections;
13 changed layouts were then captured in both orientations on `ui07`.
These checks cover entry/error states and selected scrolling paths, not every
card, file, connection or RF exchange. Sixteen mode-changing/fixture-dependent
routes were excluded from automated entry (HID/USB, immediate RF execution,
Doom direct-display handoff, companion links and active port probing).
BLE Sniffer and Exposure no longer reset on entry; the tested C5 session-start
path returned communication errors in that earlier UI pass. The subsequent
18 November C5 update corrected task memory placement and capture stream enable;
four passive capture entry/exit cycles received advertisements. See the current
[Bluetooth validation](../bluetooth/README.md). Those radio results are separate
from this navigation coverage.

On hardware, check each corrected path twice, including rapid re-entry and opening
then cancelling shutdown. For Wi-Fi, stop a scan, inspect its retained results,
then press BACK again. Do not equate these route checks with RF validation.

## Registered route inventory

“None” means no entry in `screen_close_fn`; inspect the linked source for fallback
cleanup and finite operations. This table is evidence of coverage and outstanding
lifecycle work, not a list of validated hardware behaviors.

| Route | Implementation | Explicit close hook |
|---|---|---|
| `SCREEN_WIFI_ESP_SCAN` | [ui_wifi_esp_scan_open](../../firmware_p4/components/Applications/ui/screens/wifi/ui_wifi_esp_scan.c) | `ui_wifi_esp_scan_stop` |
| `SCREEN_HOME` | [ui_home_open](../../firmware_p4/components/Applications/ui/screens/home/home_ui.c) | `ui_home_close` |
| `SCREEN_MENU` | [ui_menu_open](../../firmware_p4/components/Applications/ui/screens/menu/menu_ui.c) | `ui_menu_close` |
| `SCREEN_WIFI_MENU` | [ui_wifi_menu_open](../../firmware_p4/components/Applications/ui/screens/wifi/wifi_ui.c) | None |
| `SCREEN_WIFI_CHANNELS` | [ui_wifi_channel_open](../../firmware_p4/components/Applications/ui/screens/wifi/wifi_channel_ui.c) | None |
| `SCREEN_WIFI_CLIENTS` | [ui_wifi_client_open](../../firmware_p4/components/Applications/ui/screens/wifi/wifi_client_ui.c) | None |
| `SCREEN_WIFI_SCAN_MENU` | [ui_wifi_scan_open](../../firmware_p4/components/Applications/ui/screens/wifi/wifi_scan_ui.c) | None |
| `SCREEN_WIFI_NAMES` | [ui_wifi_names_open](../../firmware_p4/components/Applications/ui/screens/wifi/wifi_names_ui.c) | None |
| `SCREEN_CONNECT_WIFI` | [ui_connect_wifi_open](../../firmware_p4/components/Applications/ui/screens/connect_wifi/connect_wifi_ui.c) | None |
| `SCREEN_BLE_MENU` | [ui_ble_menu_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ui_ble_menu.c) | None |
| `SCREEN_BLE_SCAN` | [ui_ble_scan_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ble_scan_ui.c) | None |
| `SCREEN_GATT_EXPLORER` | [ui_gatt_explorer_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/gatt_explorer_ui.c) | None |
| `SCREEN_BLE_MOUSE_PAIRING` | [ui_ble_mouse_pairing_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ble_mouse_ui.c) | `ui_ble_mouse_stop` |
| `SCREEN_BLE_MOUSE` | [ui_ble_mouse_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ble_mouse_ui.c) | `ui_ble_mouse_stop` |
| `SCREEN_CONNECT_BLUETOOTH` | [ui_connect_bt_open](../../firmware_p4/components/Applications/ui/screens/connect_bluetooth/connect_bt_ui.c) | None |
| `SCREEN_NFC_MENU` | [ui_nfc_menu_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_menu_ui.c) | None |
| `SCREEN_NFC_EMULATE` | [ui_nfc_emulate_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_emulate_ui.c) | `ui_nfc_emulate_stop` |
| `SCREEN_NFC_READ` | [ui_nfc_read_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_read_ui.c) | `ui_nfc_read_stop` |
| `SCREEN_NFC_SAVED` | [ui_nfc_saved_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_saved_ui.c) | None |
| `SCREEN_NFC_WRITE` | [ui_nfc_write_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_write_ui.c) | `ui_nfc_write_stop` |
| `SCREEN_NFC_CONFIG` | [ui_nfc_config_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_config_ui.c) | None |
| `SCREEN_CARD_EMU` | [ui_card_emu_open](../../firmware_p4/components/Applications/ui/screens/nfc/card_emu_ui.c) | `ui_nfc_emulate_stop` |
| `SCREEN_POWER` | [ui_power_open](../../firmware_p4/components/Applications/ui/screens/power/power_ui.c) | None |
| `SCREEN_SETTINGS` | [ui_settings_open](../../firmware_p4/components/Applications/ui/screens/settings/settings_ui.c) | None |
| `SCREEN_SETTINGS_DEV` | [ui_settings_open_dev](../../firmware_p4/components/Applications/ui/screens/settings/settings_ui.c) | None |
| `SCREEN_CONNECTION_SETTINGS` | [ui_connection_settings_open](../../firmware_p4/components/Applications/ui/screens/connection_settings/connection_settings_ui.c) | None |
| `SCREEN_IR_MENU` | [ui_ir_menu_open](../../firmware_p4/components/Applications/ui/screens/infrared/ir_menu_ui.c) | None |
| `SCREEN_IR_RECEIVE` | [ui_ir_receive_open](../../firmware_p4/components/Applications/ui/screens/infrared/ir_receive_ui.c) | None |
| `SCREEN_IR_SEND` | [ui_ir_send_open](../../firmware_p4/components/Applications/ui/screens/infrared/ir_send_ui.c) | None |
| `SCREEN_IR_REMOTE_TYPE` | [ui_ir_remote_type_open](../../firmware_p4/components/Applications/ui/screens/infrared/ir_remote_type_ui.c) | None |
| `SCREEN_IR_CONTROLLER` | [ui_ir_controller_open](../../firmware_p4/components/Applications/ui/screens/infrared/ir_controller_ui.c) | None |
| `SCREEN_IR_SAVED` | [ui_ir_saved_open](../../firmware_p4/components/Applications/ui/screens/infrared/ir_saved_ui.c) | None |
| `SCREEN_IR_BURST` | [ui_ir_burst_open](../../firmware_p4/components/Applications/ui/screens/infrared/ir_burst_ui.c) | None |
| `SCREEN_HAPTIC` | [ui_haptic_open](../../firmware_p4/components/Applications/ui/screens/haptic/haptic_ui.c) | None |
| `SCREEN_SPEAKER` | [ui_speaker_open](../../firmware_p4/components/Applications/ui/screens/audio/speaker_ui.c) | None |
| `SCREEN_MIC_REC` | [ui_micrec_open](../../firmware_p4/components/Applications/ui/screens/audio/micrec_ui.c) | None |
| `SCREEN_WAV_PLAYER` | [ui_wav_player_open](../../firmware_p4/components/Applications/ui/screens/audio/wav_player_ui.c) | `ui_wav_player_stop` |
| `SCREEN_MP4_PLAYER` | [ui_mp4_player_open](../../firmware_p4/components/Applications/ui/screens/video/mp4_player_ui.c) | `ui_mp4_player_stop` |
| `SCREEN_MP3_PLAYER` | [ui_mp3_player_open](../../firmware_p4/components/Applications/ui/screens/audio/mp3_player_ui.c) | `ui_mp3_player_stop` |
| `SCREEN_PLAYER` | [ui_wav_library_open](../../firmware_p4/components/Applications/ui/screens/audio/wav_library_ui.c) | None |
| `SCREEN_IMAGE_VIEWER` | [ui_image_viewer_open](../../firmware_p4/components/Applications/ui/screens/images/image_viewer_ui.c) | `ui_image_viewer_stop` |
| `SCREEN_USB_STORAGE` | [ui_usb_storage_open](../../firmware_p4/components/Applications/ui/screens/files/usb_storage_ui.c) | `ui_usb_storage_stop` |
| `SCREEN_SPECTRUM` | [ui_spectrum_open](../../firmware_p4/components/Applications/ui/screens/audio/spectrum_ui.c) | None |
| `SCREEN_LORA_CHAT` | [ui_lora_chat_open](../../firmware_p4/components/Applications/ui/screens/lora/lora_chat_ui.c) | None |
| `SCREEN_GAMES_MENU` | [ui_games_menu_open](../../firmware_p4/components/Applications/ui/screens/games/games_menu_ui.c) | `ui_games_menu_close` |
| `SCREEN_APPS` | [ui_apps_open](../../firmware_p4/components/Applications/ui/screens/apps/apps_ui.c) | None |
| `SCREEN_SCRIPTS` | [ui_scripts_open](../../firmware_p4/components/Applications/ui/screens/dev/scripts_ui.c) | `ui_scripts_stop` |
| `SCREEN_GAME_SNAKE` | [ui_snake_open](../../firmware_p4/components/Applications/ui/screens/games/snake_ui.c) | `ui_snake_stop` |
| `SCREEN_GAME_BREAKOUT` | [ui_breakout_open](../../firmware_p4/components/Applications/ui/screens/games/breakout_ui.c) | `ui_breakout_stop` |
| `SCREEN_GAME_GB` | [ui_gb_open](../../firmware_p4/components/Applications/ui/screens/games/gb_ui.c) | None |
| `SCREEN_GAME_DOOM` | [ui_doom_real_open](../../firmware_p4/components/Applications/ui/screens/games/doom_real_ui.c) | None |
| `SCREEN_MUSE` | [ui_muse_open](../../firmware_p4/components/Applications/ui/screens/apps/muse_ui.c) | `ui_muse_stop` |
| `SCREEN_GROK_IDLE` | [ui_grok_idle_open](../../firmware_p4/components/Applications/ui/screens/apps/grok_idle_ui.c) | `ui_grok_idle_stop` |
| `SCREEN_OCTOBIT_STATUS` | [ui_octobit_status_open](../../firmware_p4/components/Applications/ui/screens/octobit/octobit_status_ui.c) | None |
| `SCREEN_DEV_MENU` | [ui_dev_menu_open](../../firmware_p4/components/Applications/ui/screens/dev/dev_menu_ui.c) | None |
| `SCREEN_SUBGHZ_MENU` | [ui_subghz_menu_open](../../firmware_p4/components/Applications/ui/screens/subghz/subghz_menu_ui.c) | None |
| `SCREEN_SUBGHZ_READ` | [ui_subghz_read_open](../../firmware_p4/components/Applications/ui/screens/subghz/subghz_read_ui.c) | `subghz_receiver_stop` |
| `SCREEN_SUBGHZ_BRUTE` | [ui_subghz_brute_open](../../firmware_p4/components/Applications/ui/screens/subghz/subghz_brute_ui.c) | None |
| `SCREEN_BLE_BEACON_SPAM` | [ui_beacon_spam_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ble_beacon_ui.c) | `ui_beacon_spam_stop` |
| `SCREEN_RFID_MENU` | [ui_rfid_menu_open](../../firmware_p4/components/Applications/ui/screens/rfid/rfid_menu_ui.c) | `ui_rfid_menu_stop` |
| `SCREEN_BADUSB_MENU` | [ui_badusb_menu_open](../../firmware_p4/components/Applications/ui/screens/badusb/badusb_menu_ui.c) | None |
| `SCREEN_GPIO` | [ui_gpio_open](../../firmware_p4/components/Applications/ui/screens/gpio/gpio_ui.c) | None |
| `SCREEN_FILES` | [ui_files_open](../../firmware_p4/components/Applications/ui/screens/files/files_ui.c) | `ui_files_close` |
| `SCREEN_WIFI_ATTACK_MENU` | [ui_wifi_attack_open](../../firmware_p4/components/Applications/ui/screens/wifi/wifi_attack_ui.c) | None |
| `SCREEN_WIFI_PACKETS_MENU` | [ui_wifi_packets_open](../../firmware_p4/components/Applications/ui/screens/wifi/wifi_packets_ui.c) | None |
| `SCREEN_WIFI_EVIL_TWIN` | [ui_wifi_evil_twin_open](../../firmware_p4/components/Applications/ui/screens/wifi/wifi_evil_twin_ui.c) | None |
| `SCREEN_COMPANION_PAIRING` | [ui_companion_pairing_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ble_companion_ui.c) | None |
| `SCREEN_BLE_SPAM_SELECT` | [ui_ble_spam_select_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ble_spam_ui.c) | None |
| `SCREEN_BLE_SPAM` | [ui_ble_spam_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ble_spam_ui.c) | `ui_ble_spam_stop` |
| `SCREEN_DISPLAY_SETTINGS` | [ui_display_settings_open](../../firmware_p4/components/Applications/ui/screens/settings/display_settings_ui.c) | None |
| `SCREEN_INTERFACE_SETTINGS` | [ui_interface_settings_open](../../firmware_p4/components/Applications/ui/screens/settings/interface_settings_ui.c) | None |
| `SCREEN_SOUND_SETTINGS` | [ui_sound_settings_open](../../firmware_p4/components/Applications/ui/screens/settings/sound_settings_ui.c) | None |
| `SCREEN_BATTERY_SETTINGS` | [ui_battery_settings_open](../../firmware_p4/components/Applications/ui/screens/settings/battery_settings_ui.c) | None |
| `SCREEN_ABOUT_SETTINGS` | [ui_about_settings_open](../../firmware_p4/components/Applications/ui/screens/settings/about_settings_ui.c) | None |
| `SCREEN_STORAGE` | [ui_storage_settings_open](../../firmware_p4/components/Applications/ui/screens/settings/storage_settings_ui.c) | None |
| `SCREEN_TIME` | [ui_time_open](../../firmware_p4/components/Applications/ui/screens/settings/time_settings_ui.c) | None |
| `SCREEN_THEME_SELECTOR` | [ui_theme_selector_open](../../firmware_p4/components/Applications/ui/screens/theme/theme_selector_ui.c) | None |
| `SCREEN_BLE_DETECT_MENU` | [ui_ble_detect_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ble_detect_ui.c) | None |
| `SCREEN_BLE_SNIFFER` | [ui_ble_sniffer_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ble_sniffer_ui.c) | `ui_ble_sniffer_stop` |
| `SCREEN_BLE_TRACKER` | [ui_ble_tracker_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ble_tracker_ui.c) | `ui_ble_tracker_stop` |
| `SCREEN_BLE_SKIMMER` | [ui_ble_skimmer_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ble_skimmer_ui.c) | `ui_ble_skimmer_stop` |
| `SCREEN_BLE_EXPOSURE` | [ui_ble_exposure_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ble_exposure_ui.c) | `ui_ble_exposure_stop` |
| `SCREEN_BLE_KEYBOARD` | [ui_ble_keyboard_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ble_keyboard_ui.c) | `ui_ble_keyboard_stop` |
| `SCREEN_BLE_FLOOD` | [ui_ble_flood_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ble_flood_ui.c) | `ui_ble_flood_stop` |
| `SCREEN_BLE_RADIO` | [ui_ble_radio_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ble_radio_ui.c) | None |
| `SCREEN_BLE_TRACK_DEVICE` | [ui_ble_track_device_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ble_track_device_ui.c) | `ui_ble_track_device_stop` |
| `SCREEN_BLE_SPAM_NAMES` | [ui_ble_spam_names_open](../../firmware_p4/components/Applications/ui/screens/bluetooth/ble_spam_names_ui.c) | None |
| `SCREEN_WIFI_PORT_SCAN` | [ui_wifi_port_scan_open](../../firmware_p4/components/Applications/ui/screens/wifi/wifi_port_scan_ui.c) | None |
| `SCREEN_WIFI_PROBE_MON` | [ui_wifi_probe_mon_open](../../firmware_p4/components/Applications/ui/screens/wifi/wifi_probe_mon_ui.c) | None |
| `SCREEN_WIFI_TARGET_CLIENTS` | [ui_wifi_target_clients_open](../../firmware_p4/components/Applications/ui/screens/wifi/wifi_target_clients_ui.c) | None |
| `SCREEN_WIFI_DEAUTH_DETECTOR` | [ui_wifi_deauth_detector_open](../../firmware_p4/components/Applications/ui/screens/wifi/wifi_deauth_detector_ui.c) | None |
| `SCREEN_WIFI_SIGNAL_LOCATOR` | [ui_wifi_signal_locator_open](../../firmware_p4/components/Applications/ui/screens/wifi/wifi_signal_locator_ui.c) | None |
| `SCREEN_NFC_SCAN` | [ui_nfc_scan_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_scan_ui.c) | `ui_nfc_scan_stop` |
| `SCREEN_SUBGHZ_SEND` | [ui_subghz_send_open](../../firmware_p4/components/Applications/ui/screens/subghz/subghz_send_ui.c) | None |
| `SCREEN_SYSTEM_UPDATE` | [ui_system_update_open](../../firmware_p4/components/Applications/ui/screens/settings/system_update_ui.c) | None |
| `SCREEN_C5_STATUS` | [ui_c5_status_open](../../firmware_p4/components/Applications/ui/screens/settings/c5_status_ui.c) | None |
| `SCREEN_C5_CONSOLE` | [ui_c5_console_open](../../firmware_p4/components/Applications/ui/screens/settings/c5_console_ui.c) | None |
| `SCREEN_LED_CTRL` | [ui_led_ctrl_open](../../firmware_p4/components/Applications/ui/screens/settings/led_ctrl_ui.c) | None |
| `SCREEN_LORA_TRACEROUTE` | [ui_lora_traceroute_open](../../firmware_p4/components/Applications/ui/screens/lora/lora_traceroute_ui.c) | None |
| `SCREEN_LORA_RNODE` | [ui_lora_rnode_open](../../firmware_p4/components/Applications/ui/screens/lora/lora_rnode_ui.c) | None |
| `SCREEN_LORA_MQTT` | [ui_lora_mqtt_open](../../firmware_p4/components/Applications/ui/screens/lora/lora_mqtt_ui.c) | None |
| `SCREEN_DEV_CONSOLE` | [ui_dev_console_open](../../firmware_p4/components/Applications/ui/screens/dev/dev_console_ui.c) | None |
| `SCREEN_PC_MONITOR` | [pc_monitor_ui_open](../../firmware_p4/components/Applications/ui/screens/dev/pc_monitor_ui.c) | `pc_monitor_ui_stop` |
| `SCREEN_DEV_DIAG` | [ui_dev_diag_open](../../firmware_p4/components/Applications/ui/screens/dev/dev_diag_ui.c) | None |
| `SCREEN_BOOT_MAP` | [ui_boot_map_open](../../firmware_p4/components/Applications/ui/screens/boot_report/boot_map_ui.c) | None |
| `SCREEN_CRASH_REPORT` | [ui_crash_report_open](../../firmware_p4/components/Applications/ui/screens/boot_report/crash_report_ui.c) | None |
| `SCREEN_NFC_BANKCARD` | [ui_nfc_bankcard_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_bankcard_ui.c) | `ui_nfc_bankcard_stop` |
| `SCREEN_NFC_DESFIRE` | [ui_nfc_desfire_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_desfire_ui.c) | `ui_nfc_protocol_lab_stop` |
| `SCREEN_NFC_ISO15693` | [ui_nfc_iso15693_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_iso15693_ui.c) | `ui_nfc_protocol_lab_stop` |
| `SCREEN_NFC_ULTRALIGHT` | [ui_nfc_ultralight_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_ultralight_ui.c) | `ui_nfc_protocol_lab_stop` |
| `SCREEN_NFC_NDEF` | [ui_nfc_ndef_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_ndef_ui.c) | `ui_nfc_protocol_lab_stop` |
| `SCREEN_NFC_PLUS` | [ui_nfc_plus_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_protocol_lab_ui.c) | `ui_nfc_protocol_lab_stop` |
| `SCREEN_NFC_ST25TB` | [ui_nfc_st25tb_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_protocol_lab_ui.c) | `ui_nfc_protocol_lab_stop` |
| `SCREEN_NFC_SLIX` | [ui_nfc_slix_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_protocol_lab_ui.c) | `ui_nfc_protocol_lab_stop` |
| `SCREEN_NFC_TEXKOM_EMULATE` | [ui_nfc_texkom_emulate_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_protocol_lab_ui.c) | `ui_nfc_protocol_lab_stop` |
| `SCREEN_NFC_V_EMULATE` | [ui_nfc_v_emulate_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_protocol_lab_ui.c) | `ui_nfc_protocol_lab_stop` |
| `SCREEN_NFC_TEXKOM` | [ui_nfc_texkom_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_protocol_lab_ui.c) | `ui_nfc_protocol_lab_stop` |
| `SCREEN_NFC_NTAG4XX` | [ui_nfc_ntag4xx_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_protocol_lab_ui.c) | `ui_nfc_protocol_lab_stop` |
| `SCREEN_NFC_ARCHIVE` | [ui_nfc_archive_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_protocol_lab_ui.c) | `ui_nfc_protocol_lab_stop` |
| `SCREEN_NFC_FELICA` | [ui_nfc_felica_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_felica_ui.c) | `ui_nfc_protocol_lab_stop` |
| `SCREEN_NFC_P2P` | [ui_nfc_p2p_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_p2p_ui.c) | `ui_nfc_p2p_stop` |
| `SCREEN_NFC_KEYDICT` | [ui_nfc_keydict_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_keydict_ui.c) | None |
| `SCREEN_NFC_APDU` | [ui_nfc_apdu_open](../../firmware_p4/components/Applications/ui/screens/nfc/nfc_apdu_ui.c) | `ui_nfc_apdu_stop` |
| `SCREEN_SUBGHZ_CONFIG` | [ui_subghz_config_open](../../firmware_p4/components/Applications/ui/screens/subghz/subghz_config_ui.c) | None |
| `SCREEN_IR_RAW` | [ui_ir_raw_open](../../firmware_p4/components/Applications/ui/screens/infrared/ir_raw_ui.c) | None |
| `SCREEN_LORA_CHANNELS` | [ui_lora_channels_open](../../firmware_p4/components/Applications/ui/screens/lora/lora_channels_ui.c) | None |
| `SCREEN_LORA_POSITION` | [ui_lora_position_open](../../firmware_p4/components/Applications/ui/screens/lora/lora_position_ui.c) | None |
| `SCREEN_LORA_TELEMETRY` | [ui_lora_telemetry_open](../../firmware_p4/components/Applications/ui/screens/lora/lora_telemetry_ui.c) | None |
| `SCREEN_LORA_SECURE_DM` | [ui_lora_securedm_open](../../firmware_p4/components/Applications/ui/screens/lora/lora_securedm_ui.c) | None |
| `SCREEN_WIFI_HANDSHAKE` | [ui_wifi_handshake_open](../../firmware_p4/components/Applications/ui/screens/wifi/wifi_handshake_ui.c) | `ui_wifi_handshake_stop` |
| `SCREEN_WIFI_HOTSPOT` | [ui_wifi_hotspot_open](../../firmware_p4/components/Applications/ui/screens/wifi/wifi_hotspot_ui.c) | `ui_wifi_hotspot_stop` |
| `SCREEN_IMU_MONITOR` | [ui_imu_monitor_open](../../firmware_p4/components/Applications/ui/screens/imu/imu_monitor_ui.c) | `ui_imu_monitor_stop` |
| `SCREEN_SD_HEALTH` | [ui_sd_health_open](../../firmware_p4/components/Applications/ui/screens/settings/sd_health_ui.c) | None |
| `SCREEN_USB_MOUSE` | [ui_usb_mouse_open](../../firmware_p4/components/Applications/ui/screens/badusb/usb_mouse_ui.c) | `ui_usb_mouse_stop` |

## UX follow-up: recent apps and capture names (2026-11-18)

Home UP, or BACK held for 900 ms and released before the existing 5 s shutdown,
opens Recent Apps: up to five actual screen previews held in checked PSRAM.
LEFT/RIGHT chooses a card, OK returns/resumes, DOWN opens Apps and short BACK
closes the overview. Closing consumes the initiating gesture so it cannot type
into an underlying editor. The current screen stays live; games pause while the
overview is open. Switching applications runs the normal resource close hooks.

Resume reconstructs the supported view: Files retains folder, selected entry,
list/grid mode and text scroll; IR Saved retains protocol/file/action level;
MQTT retains the edited field; safe RFID views and completed NFC identification
retain their data. Standard supported menus retain selection. Snake/Breakout
retain their board/score with unchanged orientation; a geometry change reopens
the app instead. Keyboard drafts in MQTT, IR Saved, RFID and completed NFC include
text, cursor, page, selected key and the original callback. Password previews mask
the temporary last character and discarded drafts are wiped. Unsupported edits
must be saved or cancelled before leaving through Recent Apps. Other operation
screens explicitly show Reopen and return to their app entry; operations are not
silently restarted or transmitted. These session states do not survive reboot.
A normal fresh app launch starts its ordinary entry and discards its old task card.

NFC Read uses the approach/return loop derived from the supplied video while the actual NFC manager
searches. The native GIF is 160x148, 12 fps, 48 palette entries, 171,553 bytes; only empty
source margins were cropped. The first 1.5 seconds play forward/backward in a
three-second loop; the success phase belongs to the actual card event. The outside background is transparent; closed dark
details in the device remain opaque. Full-canvas frames and disposal mode 2 clear
the previous silhouette without trails. ARGB8888 preserves alpha while drawing,
at its native 160x148 size (scaled down only if the body is smaller). The asset is loaded once off the LVGL task into
checked PSRAM. The flash-reading task uses an internal stack because flash I/O
disables the cache temporarily. The existing field illustration is a missing/failed-asset
fallback. All GIF timers stop on success, error and exit, before block transactions
on the shared display/NFC SPI bus. A real card-found event alone shows a green
identification check for 700 ms, then the card information for 1,400 ms and actions.
This confirms identification, not that every protected block has been read.
Retry/timeout never produces success. Restoring a completed card does not rescan.
The GIF and its notice are added to the assets LittleFS partition through a guarded
update that verifies the installed local image and preserves every existing file;
an application-only flash cannot install this asset.

IR capture Save and Sub-GHz capture Save now open the shared filename editor,
with a suggested stem and an explicit extension hint. BACK cancels the name;
Sub-GHz's existing automatic capture file remains on SD until a successful rename.
Name conflicts are reported without replacing an existing destination. Missing
Sub-GHz capture files no longer produce a false Saved confirmation. RFID read and
manual-entry saves also request a name; this library still lasts only until reboot.
NFC identification already uses the contextual name editor and retains its own
storage semantics. No folder chooser is introduced.

Diagnostics uses two columns in landscape and a bounded body above the footer;
its timer now has a central close hook and a guarded DELETE fallback. Chip labels
the die temperature explicitly. Offline LoRa telemetry retains the scroll hint.

Run test_keyboard_ui.py, then test_ux_flows.py with the same host build directory:
the latter exercises naming callbacks, actual temporary-file
renames/collisions, and the Diagnostics layout with the pinned LVGL library in both
orientations. Diagnostics timer re-entry and stale-root deletion are covered.
