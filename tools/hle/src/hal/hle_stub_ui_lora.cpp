// LoRa UI screen stubs — the radio stack (Applications/LoRa) needs the SX1262.
// Keep in sync with:
//   - firmware_p4/components/Applications/ui/ui_manager.c switch cases
//   - tools/hle/CMakeLists.txt exclusion for ui/screens/lora

extern "C" {

void ui_lora_channels_open(void) {}
void ui_lora_chat_open(void) {}
void ui_lora_mqtt_open(void) {}
void ui_lora_position_open(void) {}
void ui_lora_rnode_open(void) {}
void ui_lora_securedm_open(void) {}
void ui_lora_telemetry_open(void) {}
void ui_lora_traceroute_open(void) {}

} // extern "C"
