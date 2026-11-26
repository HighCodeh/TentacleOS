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

#include "wifi_ethernet_ui.h"

#include "esp_log.h"
#include "lvgl.h"

#include "ethernet_share.h"
#include "led_control.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

#define REFRESH_MS     250
#define CONTENT_MARGIN 12
#define LABEL_GAP      10

static const char *TAG = "ETHERNET_UI";
static lv_obj_t *s_screen, *s_content, *s_status, *s_detail, *s_traffic;
static lv_timer_t *s_timer;
static bool s_owns_session;
static esp_err_t s_start_error;

static const char *error_hint(const ethernet_share_snapshot_t *snapshot) {
  if (!s_owns_session)
    return "Previous session is still stopping. OK retries.";
  switch (snapshot->stage) {
    case ETHERNET_SHARE_STAGE_USB_CLAIM:
    case ETHERNET_SHARE_STAGE_USB_DRIVER:
      return "Close active USB functions, then retry with OK.";
    case ETHERNET_SHARE_STAGE_TYPEC:
    case ETHERNET_SHARE_STAGE_ATTACHMENT:
      return "Check the USB-C connection. Disconnect chargers before retrying.";
    case ETHERNET_SHARE_STAGE_USB_ROUTE:
      return "USB routing lost. Check the C5 connection. OK retries.";
    case ETHERNET_SHARE_STAGE_POWER_CHECK:
      if (snapshot->power_detail[0] != '\0')
        return snapshot->power_detail;
      return "Battery snapshot unavailable. OK retries.";
    case ETHERNET_SHARE_STAGE_USB_POWER:
      return "USB power check failed. Check battery charge and adapter power. OK retries.";
    case ETHERNET_SHARE_STAGE_PRESET:
      return "Turn Wi-Fi off and check the C5 connection. OK retries.";
    case ETHERNET_SHARE_STAGE_ROUTER_START:
      return "Stop active Wi-Fi operations and turn Wi-Fi off. OK retries.";
    case ETHERNET_SHARE_STAGE_ADAPTER:
      if (snapshot->error == ESP_ERR_NO_MEM)
        return "Not enough internal RAM for USB host. BACK closes sharing.";
      if (snapshot->error == ESP_ERR_TIMEOUT)
        return "USB adapter not recognized in time. Check connection and CDC-NCM support.";
      return "Adapter setup failed. CDC-NCM support is required. OK retries.";
    case ETHERNET_SHARE_STAGE_ROUTER_STATUS:
      return snapshot->error == ESP_ERR_INVALID_ARG
                 ? "Ethernet and hotspot subnets overlap. Change the hotspot subnet."
                 : "Check Ethernet DHCP and the C5 connection. OK retries.";
    default:
      return "Check the adapter and C5 connection. OK retries.";
  }
}

static void refresh(lv_timer_t *timer) {
  (void)timer;
  ethernet_share_snapshot_t snapshot;
  ethernet_share_get(&snapshot);
  if (!s_owns_session && s_start_error != ESP_OK) {
    snapshot.state = ETHERNET_SHARE_ERROR;
    snapshot.error = s_start_error;
  }
  const char *status = "Preparing USB host";
  const char *detail = "Ethernet -> Highboy -> Wi-Fi";
  switch (snapshot.state) {
    case ETHERNET_SHARE_UNPLUG:
      status = "Disconnect computer / charger";
      detail = "Then connect the Ethernet adapter to Highboy's USB-C port.";
      break;
    case ETHERNET_SHARE_WAIT_ADAPTER:
      status = "Waiting for USB adapter";
      detail = "Connect a CDC-NCM Ethernet adapter. Powered from Highboy.";
      break;
    case ETHERNET_SHARE_WAIT_LINK:
      status = "Waiting for Ethernet link";
      detail = "Connect the network cable to the adapter.";
      break;
    case ETHERNET_SHARE_DHCP:
      status = "Getting an IP address";
      detail = "Waiting for the Ethernet network's DHCP server.";
      break;
    case ETHERNET_SHARE_ONLINE:
      status = "Sharing Ethernet";
      break;
    case ETHERNET_SHARE_ERROR:
      status = "Could not start sharing";
      detail = error_hint(&snapshot);
      break;
    case ETHERNET_SHARE_STOPPING:
      status = "Stopping sharing";
      break;
    default:
      break;
  }
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_color(s_content, current_theme.bg_secondary, 0);
  lv_obj_set_style_border_color(s_content, current_theme.border_accent, 0);
  lv_obj_set_style_text_color(s_status, current_theme.text_main, 0);
  lv_obj_set_style_text_color(s_detail, current_theme.text_secondary, 0);
  lv_obj_set_style_text_color(s_traffic, current_theme.text_secondary, 0);
  lv_label_set_text(s_status, status);
  if (snapshot.state == ETHERNET_SHARE_ONLINE) {
    const uint8_t *ip = (const uint8_t *)&snapshot.ip;
    lv_label_set_text_fmt(
        s_detail, "%s\nEthernet IP: %u.%u.%u.%u", snapshot.ssid, ip[0], ip[1], ip[2], ip[3]);
    lv_label_set_text_fmt(s_traffic,
                          "Received: %lu KiB\nSent: %lu KiB",
                          (unsigned long)(snapshot.received / 1024),
                          (unsigned long)(snapshot.transmitted / 1024));
  } else {
    lv_label_set_text(s_detail, detail);
    if (snapshot.state == ETHERNET_SHARE_ERROR) {
      lv_label_set_text_fmt(s_traffic,
                            "%s\n%s",
                            s_owns_session ? ethernet_share_stage_name(snapshot.stage)
                                           : "Session start",
                            esp_err_to_name(snapshot.error));
    } else {
      lv_label_set_text(s_traffic, "Hotspot starts after Ethernet is ready.");
    }
  }
}
void ui_wifi_ethernet_stop(void) {
  if (s_timer) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  if (s_owns_session) {
    ethernet_share_stop();
    s_owns_session = false;
  }
}
static void deleted(lv_event_t *event) {
  if (lv_event_get_target(event) != s_screen)
    return;
  ui_wifi_ethernet_stop();
  s_screen = NULL;
}
static void input(const input_event_t *event, void *context) {
  (void)context;
  if (event->action != INPUT_ACTION_PRESS)
    return;
  if (event->button == INPUT_BTN_BACK) {
    ui_switch_screen(SCREEN_WIFI_HOTSPOT);
    return;
  }
  ethernet_share_snapshot_t snapshot;
  ethernet_share_get(&snapshot);
  if (event->button == INPUT_BTN_OK && s_timer != NULL &&
      (!s_owns_session || snapshot.state == ETHERNET_SHARE_ERROR)) {
    esp_err_t ret = ethernet_share_start();
    s_start_error = ret;
    if (ret == ESP_OK)
      s_owns_session = true;
    else {
      ESP_LOGE(TAG, "Retry failed: %s", esp_err_to_name(ret));
      led_signal_error();
    }
  }
}
void ui_wifi_ethernet_open(void) {
  ui_wifi_ethernet_stop();
  if (s_screen)
    lv_obj_delete(s_screen);
  s_screen = ui_chrome_screen_create();
  if (s_screen == NULL)
    return;
  lv_obj_add_event_cb(s_screen, deleted, LV_EVENT_DELETE, NULL);
  ui_chrome_header(s_screen, "Ethernet hotspot", NULL);
  ui_chrome_footer(s_screen, "BACK stops sharing");
  s_start_error = ESP_OK;
  s_content = lv_obj_create(s_screen);
  if (s_content == NULL)
    goto allocation_failed;
  lv_obj_set_pos(s_content, CONTENT_MARGIN, UI_CHROME_HEADER_H + CONTENT_MARGIN);
  lv_obj_set_size(s_content,
                  ui_screen_w() - CONTENT_MARGIN * 2,
                  ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H - CONTENT_MARGIN * 2);
  lv_obj_set_style_pad_all(s_content, CONTENT_MARGIN, 0);
  lv_obj_set_style_pad_row(s_content, LABEL_GAP, 0);
  lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
  s_status = lv_label_create(s_content);
  s_detail = lv_label_create(s_content);
  s_traffic = lv_label_create(s_content);
  if (s_status == NULL || s_detail == NULL || s_traffic == NULL)
    goto allocation_failed;
  lv_obj_set_width(s_status, LV_PCT(100));
  lv_obj_set_width(s_detail, LV_PCT(100));
  lv_obj_set_width(s_traffic, LV_PCT(100));
  s_timer = lv_timer_create(refresh, REFRESH_MS, NULL);
  if (s_timer) {
    esp_err_t ret = ethernet_share_start();
    s_start_error = ret;
    s_owns_session = ret == ESP_OK;
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Start failed: %s", esp_err_to_name(ret));
      led_signal_error();
    }
    refresh(NULL);
  } else {
    ESP_LOGE(TAG, "Refresh allocation failed");
    led_signal_error();
    lv_label_set_text(s_status, "Unavailable - BACK to exit");
  }
  ui_input_set_screen_handler(input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
  return;
allocation_failed:
  ESP_LOGE(TAG, "Screen allocation failed");
  led_signal_error();
  ui_input_set_screen_handler(input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}
