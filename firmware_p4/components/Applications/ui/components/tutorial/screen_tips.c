// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include "screen_tips.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

#include "lvgl.h"

#include "assets_manager.h"
#include "ui_theme.h"

#define TIP_NVS_NS       "scrtips"
#define TIP_NVS_KEY      "seen"
#define TIP_NVS_KEY_SKIP "skip"
#define TIP_WORDS        ((SCREEN_COUNT + 31) / 32)

#define TIP_SCRIM_OPA 232 // how dark the screen behind gets
#define TIP_ARM_MS    500 // ignore input while the intro plays
#define TIP_TEXT_W    182
#define TIP_ART       "/assets/img/image.bin"

extern lv_group_t *main_group;

static const char *TAG = "SCRTIPS";

typedef struct {
  screen_id_t id;
  const char *title;
  const char *tip;
} tip_entry_t;

static const tip_entry_t TIPS[] = {
    {SCREEN_HOME,
     "HOME",
     "Hey, I'm Octobit! This is home base. Right opens the apps, Down the settings, Left my "
     "status."},
    {SCREEN_MENU,
     "APP CAROUSEL",
     "Spin the carousel with Left/Right and press OK to open a tool. Every gadget lives here."},

    {SCREEN_WIFI_MENU,
     "WI-FI",
     "The whole Wi-Fi arsenal lives here: scan networks, map channels, hunt clients and much "
     "more."},
    {SCREEN_WIFI_ATTACK_MENU,
     "WI-FI ATTACKS",
     "Five 802.11 attacks wait here: Deauth, Beacon Spam, Probe Flood, Auth Flood and Karma."},
    {SCREEN_WIFI_HANDSHAKE,
     "WPA HANDSHAKE",
     "The trophy: I grab the WPA handshake (M1-M4) and the PMKID, saved as .pcap or .hccapx to "
     "crack later."},
    {SCREEN_WIFI_CLIENTS,
     "CLIENT MAP",
     "This map links each router to its devices; the line color tells you who is strong or weak."},
    {SCREEN_WIFI_EVIL_TWIN,
     "EVIL TWIN",
     "I raise a fake 'FreeWiFi_5G' AP to lure victims: every MAC that joins is logged and "
     "counted."},
    {SCREEN_WIFI_PACKETS_MENU,
     "PACKET SNIFFER",
     "Four sniffer modes - Raw, EAPOL, Beacon and PMKID - with live rate, all saved to a .pcap."},
    {SCREEN_WIFI_CHANNELS,
     "CHANNEL ANALYZER",
     "Each wave shows how many networks crowd a channel, and I point out the clearest one."},
    {SCREEN_WIFI_SIGNAL_LOCATOR,
     "SIGNAL LOCATOR",
     "Hunting a hidden router? The arc heats up as you get closer and tells you warm or cold."},
    {SCREEN_WIFI_DEAUTH_DETECTOR,
     "DEAUTH DETECTOR",
     "Watching channels 1-13, it counts deauth frames and flashes red if someone knocks a network "
     "offline."},

    {SCREEN_BLE_MENU,
     "BLUETOOTH",
     "The Bluetooth hub: device spam, passive detection, HID keyboard and radio control, all in "
     "one."},
    {SCREEN_GATT_EXPLORER,
     "GATT EXPLORER",
     "The GATT Explorer opens a target's services and characteristics: UUIDs, R/W/N props and live "
     "values."},
    {SCREEN_BLE_KEYBOARD,
     "BLE HID KEYBOARD",
     "Posing as a BLE keyboard, the High Boy pairs with a target and injects keys - a wireless "
     "BadUSB."},
    {SCREEN_BLE_SPAM_SELECT,
     "DEVICE SPAM",
     "Pick a profile - Apple Juice, SourApple, Android or Windows - and unleash fake BLE adverts."},
    {SCREEN_BLE_TRACKER,
     "TRACKER HUNTER",
     "The tracker hunter listens for nearby AirTags and Tiles and alarms if one seems to follow "
     "you."},

    {SCREEN_NFC_MENU,
     "NFC",
     "The whole 13.56 MHz NFC world: read tags, write, emulate and store your cards."},
    {SCREEN_CARD_EMU,
     "CARD EMULATION",
     "Build a card from scratch or pick a saved one, and the High Boy broadcasts it as the real "
     "tag."},
    {SCREEN_NFC_READ,
     "READ TAG",
     "Bring a tag close and I decode its type and UID, then dump the keys and sectors in a snap."},
    {SCREEN_NFC_WRITE,
     "WRITE TO BLANK",
     "Choose a saved card, tap a blank tag, and I copy the data onto it - a clone, ready to go."},
    {SCREEN_NFC_SCAN,
     "TECH SCAN",
     "Not sure of the tag type? This scan probes NFC-A, B, F and V and shows which protocol "
     "answers."},
    {SCREEN_NFC_BANKCARD,
     "EMV BANK CARD",
     "Here I read a contactless bank card and reveal the PAN, the network and the EMV chip's AID."},
    {SCREEN_NFC_DESFIRE,
     "MIFARE DESFIRE",
     "MIFARE DESFire opens with AES-128 auth and a CMAC session, and then I dump its files."},
    {SCREEN_NFC_P2P,
     "P2P SHARE",
     "In this mode I push an NDEF straight to a phone, negotiating LLCP and SNEP before the "
     "transfer."},
    {SCREEN_NFC_ISO15693,
     "NFC-V / ISO15693",
     "ISO15693 vicinity tags reach farther; scroll the blocks to read, write and spot the locked "
     "ones."},
    {SCREEN_NFC_KEYDICT,
     "KEY DICTIONARY",
     "The dictionary holds the MIFARE keys the reader tries; load a .dic from the SD card and edit "
     "it."},

    {SCREEN_SUBGHZ_MENU,
     "SUB-GHZ",
     "The Sub-GHz radio: capture, analyze and replay remote signals on 433, 868 and 315 MHz."},
    {SCREEN_SUBGHZ_BRUTE,
     "CODE BRUTE FORCE",
     "Brute Force fires thousands of codes across a range until one pops the gate - no key "
     "needed."},
    {SCREEN_SUBGHZ_READ,
     "CAPTURE & DECODE",
     "In Read it listens to the carrier, locks on the signal and decodes protocol, modulation, "
     "rate and key."},
    {SCREEN_RFID_MENU,
     "RFID 125 kHz",
     "RFID reads LF 125 kHz tags: read, emulate, add one by hand, even clone a whole access "
     "badge."},
    {SCREEN_SUBGHZ_CONFIG,
     "RADIO CONFIG",
     "For the tough ones, Radio Config tunes modulation, bandwidth, data rate and preset before "
     "capture."},

    {SCREEN_IR_MENU,
     "INFRARED",
     "All things infrared: capture signals, send them, act as a remote, or fire a burst."},
    {SCREEN_IR_CONTROLLER,
     "UNIVERSAL REMOTE",
     "Turns into a universal remote - a TV, audio or A/C faceplate - and OK fires the focused "
     "key."},
    {SCREEN_IR_RECEIVE,
     "LEARN A SIGNAL",
     "In Learn, aim an unknown remote: it listens, decodes the protocol and stores it to replay "
     "later."},
    {SCREEN_IR_BURST,
     "SIGNAL BURST",
     "Burst fires many saved signals back-to-back - perfect to shut off every nearby TV at once."},
    {SCREEN_IR_RAW,
     "RAW SIGNAL",
     "With no known protocol, RAW keeps the pure pulse train and lets you tune the carrier, 36 to "
     "40 kHz."},

    {SCREEN_LORA_CHAT,
     "LORA MESH",
     "Step into the LoRa mesh: pick MeshCore or Meshtastic, see the nodes on the map and chat "
     "off-grid."},
    {SCREEN_LORA_SECURE_DM,
     "ENCRYPTED DM",
     "Direct messages with per-contact X25519 keys. Compare fingerprints to be sure who is on the "
     "other side."},
    {SCREEN_LORA_TRACEROUTE,
     "MESH TRACEROUTE",
     "Want your message's path? Traceroute maps every hop and shows the SNR of each leg."},
    {SCREEN_LORA_RNODE,
     "RNODE / KISS",
     "In RNode mode the radio becomes a KISS modem: set frequency, SF and power, count raw RX/TX "
     "packets."},
    {SCREEN_LORA_MQTT,
     "MQTT BRIDGE",
     "The MQTT bridge links your mesh to the internet: set broker, user and password, then "
     "connect."},

    {SCREEN_BADUSB_MENU,
     "BADUSB",
     "Here the High Boy becomes a fake USB keyboard: run payloads, pick scripts and drive the HID "
     "mouse."},
    {SCREEN_BADUSB_RUNNING,
     "RUN PAYLOAD",
     "Once plugged in, it types on its own like a keyboard, opens a terminal and injects the "
     "payload."},
    {SCREEN_BADUSB_BROWSER,
     "PAYLOAD LIBRARY",
     "Each .duck file holds a keystroke script; preview it before you launch the attack."},
    {SCREEN_USB_MOUSE,
     "HID MOUSE",
     "It also becomes a USB mouse: move the cursor, click, scroll, and the jiggler keeps the "
     "screen awake."},
    {SCREEN_BADUSB_LAYOUT,
     "KEYBOARD LAYOUT",
     "The layout must match the target keyboard: pick US, UK, DE, FR or BR so keys don't come out "
     "wrong."},

    {SCREEN_DEV_MENU,
     "DEVELOPER",
     "The developer area: Scripts, Console, P4 Update and Diagnostics, all in one place."},
    {SCREEN_SCRIPTS,
     "SCRIPTS",
     "The .js scripts show capability badges; if one wants USB or Wi-Fi, I ask your permission "
     "first."},
    {SCREEN_STORAGE,
     "STORAGE",
     "See real SD and internal memory usage here. Careful: formatting wipes the whole card for "
     "good."},
    {SCREEN_SYSTEM_UPDATE,
     "P4 UPDATE",
     "This screen fetches new firmware, installs the P4 update and reboots on its own. Don't power "
     "off."},
    {SCREEN_FILES,
     "FILES",
     "This browser walks your real files: internal memory and the SD card, in list or grid view."},

    {SCREEN_GPIO,
     "GPIO BUS",
     "The GPIO bus shows IO1-IO8, 5V and UART as green LEDs. Select a pin and press OK to toggle "
     "it."},
    {SCREEN_HAPTIC,
     "HAPTIC BENCH",
     "The vibration bench drives the real DRV2605L motor: pick effects, fire patterns and "
     "calibrate the ERM."},
    {SCREEN_SPEAKER,
     "SPEAKER",
     "On the speaker I synth chiptunes note by note over I2S: Mario, Tetris and a live 10-band "
     "equalizer."},
    {SCREEN_MIC_REC,
     "RECORDER",
     "Capture up to 5s from the mic and play it back on the speaker, watching the live VU and "
     "waveform."},

    {SCREEN_SETTINGS,
     "SETTINGS",
     "All your settings: connections, display, sound, power and system. Pick a section and I'll "
     "explain."},
    {SCREEN_POWER,
     "POWER CONSOLE",
     "Here I talk to the BQ25896 chip live: battery voltage, current and faults, plus I2C scan and "
     "power off."},
    {SCREEN_OCTOBIT_STATUS,
     "OCTOBIT STATUS",
     "This is my status card: level, XP and real device stats like boots, battery and free "
     "memory."},
    {SCREEN_THEME_SELECTOR,
     "THEMES",
     "Spin the carousel to switch theme: 12 palettes repaint the whole UI. Press OK to make it "
     "yours."},
    {SCREEN_COMPANION_PAIRING,
     "PAIR THE APP",
     "To hook up the phone app, this screen shows the Bluetooth pairing code. Confirm in the app "
     "to connect."},
};

#define TIP_COUNT ((int)(sizeof(TIPS) / sizeof(TIPS[0])))

static bool s_active = false;
static bool s_seen_loaded = false;
static bool s_skip = false;
static uint32_t s_seen[TIP_WORDS];
static lv_obj_t *s_scrim = NULL;
static lv_obj_t *s_prev_focus = NULL;
static uint32_t s_open_tick = 0;

static const tip_entry_t *tip_for(screen_id_t screen) {
  for (int i = 0; i < TIP_COUNT; i++) {
    if (TIPS[i].id == screen)
      return &TIPS[i];
  }
  return NULL;
}

static void seen_load(void) {
  if (s_seen_loaded)
    return;
  memset(s_seen, 0, sizeof(s_seen));
  nvs_handle_t h;
  if (nvs_open(TIP_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
    size_t len = sizeof(s_seen);
    nvs_get_blob(h, TIP_NVS_KEY, s_seen, &len);
    uint8_t skip = 0;
    nvs_get_u8(h, TIP_NVS_KEY_SKIP, &skip);
    s_skip = (skip != 0);
    nvs_close(h);
  }
  s_seen_loaded = true;
}

static bool seen_get(screen_id_t screen) {
  uint32_t idx = (uint32_t)screen;
  if (idx >= (uint32_t)SCREEN_COUNT)
    return true;
  return (s_seen[idx / 32] >> (idx % 32)) & 1u;
}

static void seen_mark(screen_id_t screen) {
  uint32_t idx = (uint32_t)screen;
  if (idx >= (uint32_t)SCREEN_COUNT)
    return;
  s_seen[idx / 32] |= (1u << (idx % 32));
  nvs_handle_t h;
  if (nvs_open(TIP_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_blob(h, TIP_NVS_KEY, s_seen, sizeof(s_seen));
    nvs_commit(h);
    nvs_close(h);
  }
}

static void hijack_input(void) {
  if (main_group == NULL)
    return;
  lv_obj_t *cur = lv_group_get_focused(main_group);
  if (cur != NULL && cur != s_scrim)
    s_prev_focus = cur;
  lv_group_remove_all_objs(main_group);
  lv_group_add_obj(main_group, s_scrim);
  lv_group_focus_obj(s_scrim);
  lv_group_set_editing(main_group, false);
}

static void dismiss(void) {
  if (!s_active)
    return;
  s_active = false;

  lv_obj_t *scrim = s_scrim;
  lv_obj_t *restore = s_prev_focus;
  s_scrim = NULL;
  s_prev_focus = NULL;

  if (main_group != NULL && scrim != NULL)
    lv_group_remove_obj(scrim);
  if (scrim != NULL)
    lv_obj_del_async(scrim);
  if (main_group != NULL && restore != NULL) {
    lv_group_add_obj(main_group, restore);
    lv_group_focus_obj(restore);
  }
}

static void scrim_key_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;
  if (lv_tick_get() - s_open_tick < TIP_ARM_MS)
    return;
  uint32_t key = lv_event_get_key(e);
  if (key == LV_KEY_ENTER || key == LV_KEY_ESC || key == LV_KEY_RIGHT)
    dismiss();
}

void screen_tips_handle_input(const input_event_t *ev) {
  if (!s_active || ev == NULL)
    return;
  if (ev->action != INPUT_ACTION_PRESS)
    return;
  if (lv_tick_get() - s_open_tick < TIP_ARM_MS)
    return;
  if (ev->button == INPUT_BTN_OK || ev->button == INPUT_BTN_BACK || ev->button == INPUT_BTN_RIGHT)
    dismiss();
}

static void tip_opa_cb(void *o, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
}
static void tip_bgopa_cb(void *o, int32_t v) {
  lv_obj_set_style_bg_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
}
static void tip_ty_cb(void *o, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)o, v, 0);
}

// Fade an element in from transparent, after `delay` ms.
static void tip_fade(lv_obj_t *o, uint32_t delay) {
  lv_obj_set_style_opa(o, LV_OPA_TRANSP, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, tip_opa_cb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&a, 280);
  lv_anim_set_delay(&a, delay);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

// Gentle continuous float for the mascot.
static void tip_bob(lv_obj_t *o) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, tip_ty_cb);
  lv_anim_set_values(&a, -5, 5);
  lv_anim_set_duration(&a, 1600);
  lv_anim_set_reverse_duration(&a, 1600);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

static void build_overlay(const tip_entry_t *entry) {
  lv_obj_t *scrim = lv_obj_create(lv_layer_top());
  lv_obj_set_size(scrim, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(scrim, 0, 0);
  lv_obj_remove_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(scrim, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scrim, LV_OPA_TRANSP, 0); // fades in — the screen darkens
  lv_obj_set_style_border_width(scrim, 0, 0);
  lv_obj_set_style_radius(scrim, 0, 0);
  lv_obj_set_style_pad_all(scrim, 0, 0);
  lv_obj_add_event_cb(scrim, scrim_key_cb, LV_EVENT_KEY, NULL);
  s_scrim = scrim;

  // The screen behind darkens first.
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, scrim);
  lv_anim_set_exec_cb(&a, tip_bgopa_cb);
  lv_anim_set_values(&a, 0, TIP_SCRIM_OPA);
  lv_anim_set_duration(&a, 240);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);

  // No boxed window: octobit on top, the explanation below it, centered on the scrim.
  lv_obj_t *col = lv_obj_create(scrim);
  lv_obj_remove_style_all(col);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(col, LV_PCT(100));
  lv_obj_set_height(col, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 11, 0);
  lv_obj_center(col);

  lv_image_dsc_t *dsc = assets_get(TIP_ART);
  if (dsc != NULL) {
    lv_obj_t *img = lv_image_create(col);
    lv_image_set_src(img, dsc);
    lv_image_set_scale(img, 168); // ~66% so it leaves room for the text
    tip_fade(img, 130);
    tip_bob(img);
  }

  lv_obj_t *title = lv_label_create(col);
  lv_label_set_text(title, entry->title);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(title, current_theme.border_accent, 0);
  tip_fade(title, 300);

  lv_obj_t *tip = lv_label_create(col);
  lv_label_set_long_mode(tip, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(tip, TIP_TEXT_W);
  lv_label_set_text(tip, entry->tip);
  lv_obj_set_style_text_font(tip, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(tip, current_theme.text_main, 0);
  lv_obj_set_style_text_align(tip, LV_TEXT_ALIGN_CENTER, 0);
  tip_fade(tip, 380);

  lv_obj_t *hint = lv_label_create(col);
  lv_label_set_text(hint, LV_SYMBOL_OK "  OK");
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(hint, current_theme.border_accent, 0);
  lv_obj_set_style_text_opa(hint, 180, 0);
  tip_fade(hint, 460);

  s_open_tick = lv_tick_get();
  s_active = true;
  hijack_input();
}

void screen_tips_hook(screen_id_t screen) {
  if (s_active) {
    hijack_input();
    return;
  }
  const tip_entry_t *entry = tip_for(screen);
  if (entry == NULL)
    return;
  seen_load();
  if (s_skip || seen_get(screen))
    return;
  seen_mark(screen);
  ESP_LOGI(TAG, "tip for screen %d: %s", (int)screen, entry->title);
  build_overlay(entry);
}

bool screen_tips_active(void) {
  return s_active;
}

void screen_tips_reset(void) {
  memset(s_seen, 0, sizeof(s_seen));
  s_seen_loaded = true;
  nvs_handle_t h;
  if (nvs_open(TIP_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_blob(h, TIP_NVS_KEY, s_seen, sizeof(s_seen));
    nvs_commit(h);
    nvs_close(h);
  }
}
