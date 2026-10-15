// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "console_service.h"

#include <stdio.h>
#include <string.h>

#include "esp_console.h"

#include "battery_service.h"
#include "bq25896.h"

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
      return "USB host / SDP";
    case VBUS_STATUS_ADAPTER_PORT:
      return "adapter";
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
  if (argc > 1 && strcmp(argv[1], "regs") == 0) {
    return print_registers();
  }

  if (!bq25896_is_present()) {
    printf("Battery: BQ25896 did not identify at I2C address 0x%02X\n", BQ25896_I2C_ADDR);
    return 1;
  }

  bq25896_telem_t t;
  esp_err_t err = bq25896_read_telemetry(&t);
  if (err != ESP_OK) {
    printf("Battery: telemetry read failed: %s\n", esp_err_to_name(err));
    return 1;
  }

  battery_snapshot_t bs;
  bool have_snap = battery_service_get(&bs);
  int temp_abs = t.temperature_decic < 0 ? -t.temperature_decic : t.temperature_decic;

  printf("BQ25896 silicon\n");
  printf("  Identity : PN=%u (BQ25896), revision=%u, REG14=0x%02X\n",
         t.part_number,
         t.device_revision,
         t.raw_part_info);
  printf("  ADC      : %s, TS=%lu.%03lu %%REGN\n",
         t.adc_continuous ? "continuous" : "NOT continuous",
         (unsigned long)(t.ts_milli_pct / 1000),
         (unsigned long)(t.ts_milli_pct % 1000));

  printf("Battery / system\n");
  printf("  SoC      : %u %% voltage estimate", t.soc);
  if (have_snap && bs.valid) {
    printf(" (smoothed %d %%)", bs.soc);
  }
  printf("\n");
  printf("  VBAT     : %u mV\n", t.vbat_mv);
  printf("  VSYS     : %u mV%s\n", t.vsys_mv, t.vsys_regulation ? " (VSYSMIN regulation)" : "");
  printf("  Temp     : %s%d.%d C (NTC estimate, %s)\n",
         t.temperature_decic < 0 ? "-" : "",
         temp_abs / 10,
         temp_abs % 10,
         ntc_name(t.ntc_state));
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
      .help = "BQ25896 telemetry and limits; use 'battery regs' for REG00..REG14",
      .hint = "[regs]",
      .func = &cmd_battery,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd));
}
