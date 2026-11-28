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

// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "console_service.h"

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"

#include "battery_service.h"
#include "bq27220.h"
#include "bq25896.h"
#include "power_profile.h"
#include "usb_power.h"
#include "led_control.h"
#include "tos_config.h"

static const char *TAG = "BATTERY_PROFILE";

static void show_profile(const battery_service_profile_t *profile) {
  ESP_LOGI(TAG,
           "Profile: %s (%s), %u mAh, %u mV nominal",
           profile->id,
           profile->name,
           profile->capacity_mah,
           profile->nominal_mv);
  if (profile->specified_recovery_ma != 0)
    ESP_LOGI(TAG,
             "Cell recovery specification: %u mA; not supported by retained charger policy",
             profile->specified_recovery_ma);
  else
    ESP_LOGI(TAG, "Cell recovery specification unavailable");
  ESP_LOGI(TAG,
           "Charge policy: %u-4200 mV, <=384 mA / 4176 mV; low-cell recovery "
           "unavailable; gauge unqualified",
           power_profile_charge_min_mv(profile->charge_profile));
}

static int profile_command(int argc, char **argv) {
  if (strcmp(argv[1], "profiles") == 0 && argc == 2) {
    const battery_service_profile_t *active = battery_service_profile_get();
    const battery_service_profile_t *profile;
    for (uint32_t index = 0; (profile = battery_service_profile_at(index)) != NULL; ++index)
      ESP_LOGI(TAG,
               "%s %s: %s, %u mAh, %u mV nominal",
               profile == active ? "*" : " ",
               profile->id,
               profile->name,
               profile->capacity_mah,
               profile->nominal_mv);
    return 0;
  }
  if (strcmp(argv[1], "profile") == 0) {
    if (argc == 2) {
      show_profile(battery_service_profile_get());
      return 0;
    }
    if (argc == 3) {
      esp_err_t result = tos_config_set_battery_profile(argv[2]);
      if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "Battery profile not saved: %s; use battery profiles for valid IDs",
                 esp_err_to_name(result));
        led_signal_error();
        return 1;
      }
      ESP_LOGI(TAG, "Battery profile saved and selected");
      led_signal_info();
      show_profile(battery_service_profile_get());
      return 0;
    }
  }
  ESP_LOGW(TAG, "Usage: battery profiles | battery profile [ID]");
  led_signal_warning();
  return 1;
}

static const char *chg_name(bq25896_charge_status_t state) {
  switch (state) {
    case CHARGE_STATUS_PRECHARGE:
      return "pre-charge";
    case CHARGE_STATUS_FAST_CHARGE:
      return "fast-charge";
    case CHARGE_STATUS_CHARGE_DONE:
      return "done";
    default:
      return "idle";
  }
}

static const char *vbus_name(bq25896_vbus_status_t state) {
  switch (state) {
    case VBUS_STATUS_USB_HOST:
      return "USB500 (PSEL)";
    case VBUS_STATUS_ADAPTER_PORT:
      return "adapter (PSEL)";
    case VBUS_STATUS_OTG:
      return "OTG output";
    default:
      return "none";
  }
}

static const char *ntc_name(bq25896_ntc_state_t state) {
  switch (state) {
    case BQ25896_NTC_WARM:
      return "warm";
    case BQ25896_NTC_COOL:
      return "cool";
    case BQ25896_NTC_COLD:
      return "cold";
    case BQ25896_NTC_HOT:
      return "hot";
    default:
      return "normal";
  }
}

static const char *charge_fault_name(bq25896_charge_fault_t fault) {
  switch (fault) {
    case BQ25896_CHARGE_FAULT_INPUT:
      return "input";
    case BQ25896_CHARGE_FAULT_THERMAL:
      return "thermal shutdown";
    case BQ25896_CHARGE_FAULT_TIMER:
      return "safety timer";
    default:
      return "none";
  }
}

static int print_registers(void) {
  uint8_t regs[BQ25896_REGISTER_COUNT] = {0};
  esp_err_t err = bq25896_dump_registers(regs);
  if (err != ESP_OK) {
    printf("BQ25896 register read failed: %s\n", esp_err_to_name(err));
    return 1;
  }

  printf("BQ25896 raw registers (REG0C is the second/live read)\n");
  for (uint8_t i = 0; i < BQ25896_REGISTER_COUNT; i++) {
    printf("  %02X:%02X%s", i, regs[i], ((i + 1) % 7 == 0 || i == 0x14) ? "\n" : "  ");
  }
  return 0;
}

static int cmd_battery(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "repair") == 0) {
    esp_err_t error = battery_service_restore_profile();
    if (error == ESP_OK) {
      ESP_LOGI(TAG,
               "Confirmed battery profile restored and verified; charging paused. Reboot, then "
               "check battery.");
      led_signal_info();
    } else {
      ESP_LOGE(
          TAG, "Battery profile restore failed: %s; check battery status", esp_err_to_name(error));
      led_signal_error();
    }
    return error == ESP_OK ? 0 : 1;
  }
  if (argc >= 2 && (strcmp(argv[1], "profiles") == 0 || strcmp(argv[1], "profile") == 0))
    return profile_command(argc, argv);
  if (argc == 3 && strcmp(argv[1], "charge") == 0 &&
      (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
    esp_err_t error = battery_service_request_charge(strcmp(argv[2], "on") == 0);
    printf("Charge request queued: %s; activation requires valid telemetry and limits\n",
           esp_err_to_name(error));
    return error == ESP_OK ? 0 : 1;
  }
  if (argc != 1 && !(argc == 2 && strcmp(argv[1], "regs") == 0)) {
    ESP_LOGW(TAG, "Usage: battery [regs|profiles|profile [ID]|repair|charge on|charge off]");
    led_signal_warning();
    return 1;
  }
  show_profile(battery_service_profile_get());
  battery_charge_snapshot_t service = {0};
  bool have_service = battery_service_get_charge(&service);
  usb_power_snapshot_t usb;
  if (usb_power_get(&usb)) {
    ESP_LOGI(TAG,
             "USB-C: valid=%d attached=%d advertised=%u mA (0=default/unknown); "
             "source=%s last_reg=0x%02X; input=%u mA control=%s",
             usb.is_source_valid,
             usb.is_attached,
             usb.advertised_ma,
             esp_err_to_name(usb.source_error),
             usb.source_reg,
             usb.applied_ma,
             esp_err_to_name(usb.input_error));
  } else {
    ESP_LOGI(TAG, "USB-C input policy unavailable");
  }
  printf("Charge service: requested=%d valid=%d status=%s\n",
         service.requested,
         have_service,
         service.status ? service.status : "not ready");
  if (argc > 1 && strcmp(argv[1], "regs") == 0) {
    return print_registers();
  }

  if (!bq25896_is_present()) {
    printf("Battery: BQ25896 did not identify at I2C address 0x%02X\n", BQ25896_I2C_ADDR);
    return 1;
  }

  bq25896_telem_t t;
  esp_err_t err = ESP_OK;
#ifdef CONFIG_HIGHBOY_BOARD_V203
  if (have_service)
    t = service.charger;
  else
    err = bq25896_read_telemetry(&t);
#else
  err = bq25896_read_telemetry(&t);
#endif
  if (err != ESP_OK) {
    printf("Battery: telemetry read failed: %s\n", esp_err_to_name(err));
    return 1;
  }

#ifdef CONFIG_HIGHBOY_BOARD_V203
  ESP_LOGW(TAG,
           "Retained limits: current=%s voltage=%s; temperature UNVALIDATED; "
           "guarded charge service; board sensor is not cell temperature",
           t.charge_current_limit_ma <= POWER_PROFILE_CHARGE_STANDARD_MA ? "within standard"
                                                                         : "ABOVE standard",
           t.charge_voltage_limit_mv <= POWER_PROFILE_CHARGE_MAX_MV ? "within ceiling"
                                                                    : "ABOVE ceiling");
#endif

#ifndef CONFIG_HIGHBOY_BOARD_V203
  battery_snapshot_t bs;
  bool have_snap = battery_service_get(&bs);
  int temp_abs = t.temperature_decic < 0 ? -t.temperature_decic : t.temperature_decic;
#endif

  printf("BQ25896 silicon\n");
  printf("  Identity : PN=%u (BQ25896), revision=%u, REG14=0x%02X\n",
         t.part_number,
         t.device_revision,
         t.raw_part_info);
  printf("  ADC      : %s, TS=%lu.%03lu %%REGN\n",
         t.adc_valid ? (t.adc_continuous ? "continuous" : "fresh one-shot complete") : "INVALID",
         (unsigned long)(t.ts_milli_pct / 1000),
         (unsigned long)(t.ts_milli_pct % 1000));

  printf("Battery / system\n");
  if (have_service && service.soc >= 0)
    ESP_LOGI(TAG,
             "Battery charge: %d%% (%s)",
             service.soc,
             service.gauge_valid ? "gauge" : "filtered voltage estimate");
#ifdef CONFIG_HIGHBOY_BOARD_V203
  bq27220_sample_t gauge;
  if (bq27220_read_sample(&gauge) == ESP_OK) {
    ESP_LOGI(TAG,
             "BQ27220 raw SoC=%u%% (UNQUALIFIED), remaining=%u/%u mAh, design=%u mAh, net=%d mA, "
             "VBAT=%u mV",
             gauge.soc_pct,
             gauge.remaining_mah,
             gauge.full_mah,
             gauge.design_mah,
             gauge.current_ma,
             gauge.voltage_mv);
  } else {
    ESP_LOGW(TAG, "BQ27220 unavailable; using voltage estimate when telemetry is valid");
  }
#else
  printf("  SoC      : %u %% voltage estimate", t.soc);
  if (have_snap && bs.valid)
    printf(" (smoothed %d %%)", bs.soc);
  printf("\n");
#endif
  printf("  VBAT     : %u mV\n", t.vbat_mv);
  printf("  VSYS     : %u mV%s\n", t.vsys_mv, t.vsys_regulation ? " (VSYSMIN regulation)" : "");
#ifdef CONFIG_HIGHBOY_BOARD_V203
  printf("  Temp     : not calibrated; TS ratio above; hardware NTC=%s\n", ntc_name(t.ntc_state));
#else
  printf("  Temp     : %s%d.%d C (NTC estimate, %s)\n",
         t.temperature_decic < 0 ? "-" : "",
         temp_abs / 10,
         temp_abs % 10,
         ntc_name(t.ntc_state));
#endif
  printf("  Charge   : %s, %s, measured %u mA\n",
         t.charge_enabled ? "enabled" : "disabled",
         chg_name(t.chg),
         t.ichg_ma);
  printf("  Limits   : ICHG=%u mA, pre=%u mA, term=%u mA, VREG=%u mV, "
         "SYS_MIN=%u mV\n",
         t.charge_current_limit_ma,
         t.precharge_current_limit_ma,
         t.termination_current_limit_ma,
         t.charge_voltage_limit_mv,
         t.system_min_voltage_mv);

  printf("Input / boost\n");
  printf("  Source   : %s, PG=%s, VBUS_GD=%s, VBUS=%u mV\n",
         vbus_name(t.vbus),
         t.power_good ? "yes" : "no",
         t.vbus_good ? "yes" : "no",
         t.vbus_mv);
  printf("  Input    : IINLIM=%u mA, effective=%u mA, VINDPM=%u mV, ICO=%s/%s\n",
         t.input_current_limit_ma,
         t.effective_input_limit_ma,
         t.input_voltage_limit_mv,
         t.ico_enabled ? "on" : "off",
         t.ico_optimized ? "optimized" : "learning");
  printf("  DPM      : current=%s, voltage=%s, HIZ=%s, thermal=%s\n",
         t.input_current_dpm ? "active" : "clear",
         t.input_voltage_dpm ? "active" : "clear",
         t.hiz_enabled ? "on" : "off",
         t.thermal_regulation ? "active" : "clear");
  printf("  OTG      : %s, profile %u mV / %u mA\n",
         t.otg_enabled ? "enabled" : "disabled",
         t.boost_voltage_mv,
         t.boost_current_limit_ma);

  printf("Faults\n");
  printf("  REG0C    : latched=0x%02X, live=0x%02X\n", t.fault_latched, t.fault_live);
  printf("  Decode   : charge=%s, watchdog=%s, boost=%s, BAT_OVP=%s, NTC=%s\n",
         charge_fault_name(t.charge_fault),
         t.watchdog_fault ? "FAULT" : "ok",
         t.boost_fault ? "FAULT" : "ok",
         t.battery_ovp ? "FAULT" : "ok",
         ntc_name(t.ntc_state));
  return 0;
}

void register_battery_commands(void) {
  const esp_console_cmd_t cmd = {
      .command = "battery",
      .help = "Battery telemetry, guarded charge control and persistent profile selection",
      .hint = "[regs|profiles|profile [ID]|charge on|charge off]",
      .func = &cmd_battery,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd));
}
