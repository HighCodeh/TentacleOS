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

#include "rfid_lab.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/lp_system_reg.h"
#include "soc/soc.h"

#include "pin_def.h"
#include "rfid_lab_command.h"
#include "rfid_lab_io.h"
#include "rfid_lab_protocol.h"

static const char *TAG = "RFID_LAB";
#define UART_BAUD 115200U
#define UART_RX_BYTES 1024U
#define REPLY_BYTES 512U
#define POLL_MS 5U
#define REPORT_US 500000U
#define PRECHECK_US 50000U
#define SWEEP_GAP_US 250000U
#define READ_SETTLE_US 450000U
#define US_PER_MS 1000U
#define US_PER_SECOND 1000000U
#define EXTERNAL_PRESENT_HZ 10000U
#define EXTERNAL_MIN_HZ 110000U
#define EXTERNAL_MAX_HZ 140000U
#define ASCII_CTRL_C 3U
#define ASCII_BACKSPACE 8U
#define ASCII_DELETE 127U
#define ASCII_CTRL_U 21U
#define ASCII_SPACE 32U
#define HEX_BYTES 65U
#define HASH_BYTES 32U
#define HEX_DIGITS 2U
#define HEX_BYTE_SIZE 3U
#define CAPTURE_DRAIN_BATCHES 8U
#define REBOOT_DELAY_MS 100U

typedef enum {
  RFID_LAB_IDLE = 0, RFID_LAB_PRECHECK, RFID_LAB_RUNNING, RFID_LAB_GAP,
  RFID_LAB_FAULT, RFID_LAB_PHASE_COUNT
} rfid_lab_phase_t;
static rfid_lab_phase_t s_phase;
static rfid_lab_command_t s_command;
static int64_t s_deadline;
static int64_t s_started;
static int64_t s_report_at;
static rfid_lab_feedback_t s_before;
static rfid_lab_feedback_t s_previous_report;
static rfid_lab_decoder_t s_decoder;
// Bounded diagnostic samples retained only until the next command.
static rfid_lab_edge_t s_raw[RFID_LAB_RAW_CAPACITY];
static size_t s_raw_count;
static bool s_reported_id;
static bool s_uses_capture;

static void reply(const char *format, ...) {
  char buffer[REPLY_BYTES];
  va_list args;
  va_start(args, format);
  int length = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (length > 0) uart_write_bytes(UART_NUM_0, buffer,
      (size_t)length < sizeof(buffer) ? (size_t)length : sizeof(buffer) - 1U);
}

static void prompt(void) { reply("rfid> "); }

static uint32_t frequency(const rfid_lab_feedback_t *after, const rfid_lab_feedback_t *before) {
  int64_t elapsed = after->time_us - before->time_us;
  if (elapsed <= 0) return 0;
  return (uint64_t)(after->cl_rises - before->cl_rises) * US_PER_SECOND / elapsed;
}

static void pins(void) {
  reply("GPIO2 = carrier drive (reader). GPIO11 = load modulation.\r\n"
        "GPIO15 = CL carrier INPUT. GPIO16 = DA data INPUT.\r\n"
        "UART0 TX=38 RX=37; USB CP2105 P4, 115200 8N1, no flow control.\r\n");
}

static void help(void) {
  reply("HIGH BOY RFID LAB - type one command, then Enter\r\n"
        "  field [kHz duty%% ms]   default: field 125 50 5000\r\n"
        "  pulse [Hz duty%% ms]   GPIO11 only; default: pulse 1000 50 1000\r\n"
        "  sweep [first_kHz last_kHz step_kHz ms]\r\n"
        "                         default: sweep 20 200 10 5000\r\n");
  reply("  gpio 2|11 0|1 [ms]     static level; default 100 ms, max 1000 ms\r\n"
        "  listen [ms]            passive CL/DA; default 1000 ms\r\n"
        "  read [ms]              EM4100 RF/64 + carrier; default 5000 ms\r\n"
        "  raw [ms]               carrier + DA pulse dump; default 1000 ms\r\n"
        "  emulate [ms] [0|1]     test ID 0102030405; external reader required\r\n");
  reply("  status | pins | info | off | help | reboot\r\n"
        "  off or Ctrl+C stops; outputs return LOW at each deadline.\r\n"
        "  field: 20-200 kHz; pulse: 1-10000 Hz; duty: 10-90%%; time: 20-5000 ms.\r\n"
        "  CL frequency is digital feedback, NOT coil amplitude/resonance.\r\n");
}

static void status(void) {
  rfid_lab_feedback_t feedback;
  if (rfid_lab_io_feedback(&feedback) != ESP_OK) {
    reply("ERROR feedback unavailable\r\n");
    return;
  }
  const char *state = s_phase == RFID_LAB_IDLE ? "OFF" : s_phase == RFID_LAB_RUNNING ? "ACTIVE" :
    s_phase == RFID_LAB_FAULT ? "FAULT - RESET REQUIRED" : "OUTPUTS OFF / WAIT";
  reply("STATUS %s CL=%d DA=%d CL_total=%" PRIu32 " DA_total=%" PRIu32 " dropped=%" PRIu32 "\r\n",
    state, feedback.cl_level, feedback.da_level, feedback.cl_rises, feedback.da_edges, feedback.dropped);
}

static void drain_capture(void) {
  if (!s_uses_capture) return;
  rfid_lab_edge_t edges[RFID_LAB_SAMPLE_BATCH];
  size_t count = rfid_lab_io_pop(edges, RFID_LAB_SAMPLE_BATCH);
  bool is_settled = s_command.kind != RFID_LAB_READ || esp_timer_get_time() - s_started >= READ_SETTLE_US;
  for (size_t i = 0; i < count; i++) {
    if (s_raw_count < RFID_LAB_RAW_CAPACITY) s_raw[s_raw_count++] = edges[i];
    if (is_settled) rfid_lab_protocol_feed(&s_decoder, edges[i].duration_us, edges[i].level);
  }
}

static void finish(const char *reason) {
  esp_err_t off_error = rfid_lab_io_off();
  esp_err_t capture_error = rfid_lab_io_capture(false);
  rfid_lab_feedback_t feedback = {0};
  esp_err_t feedback_error = rfid_lab_io_feedback(&feedback);
  // Drain only the bounded tail, after disabling acquisition and the outputs.
  for (size_t batch = 0; batch < CAPTURE_DRAIN_BATCHES; batch++) drain_capture();
  bool was_active = s_phase == RFID_LAB_RUNNING;
  s_phase = off_error == ESP_OK && capture_error == ESP_OK ? RFID_LAB_IDLE : RFID_LAB_FAULT;
  reply("\r\n%s: %s\r\n", reason, s_phase == RFID_LAB_IDLE ? "GPIO2=LOW GPIO11=LOW" : "OFF FAILED; RESET REQUIRED");
  if (was_active && feedback_error == ESP_OK) {
    reply("RESULT CL=%" PRIu32 " Hz; DA_edges=%" PRIu32 "; captured=%" PRIu32 "; dropped=%" PRIu32 "\r\n",
      frequency(&feedback, &s_before), feedback.da_edges - s_before.da_edges,
      s_uses_capture ? feedback.capture_edges : 0, s_uses_capture ? feedback.dropped : 0);
    if (s_uses_capture) {
      if (s_decoder.is_valid && feedback.dropped == 0)
        reply("EM4100 RF/64 ID=%010" PRIX64 " (3 matching frames). Verify with reference reader.\r\n", s_decoder.id);
      else reply("NO VALID EM4100 ID; invalid_intervals=%" PRIu32 "%s\r\n", s_decoder.invalid_pulses,
        feedback.dropped ? "; CAPTURE OVERFLOW - INVALID TEST" : "");
    }
    if (s_command.kind == RFID_LAB_RAW) {
      reply("RAW first %u intervals: duration_us, completed_level\r\n", (unsigned)s_raw_count);
      for (size_t i = 0; i < s_raw_count; i++) reply("%" PRIu32 ",%u\r\n", s_raw[i].duration_us, s_raw[i].level);
    }
  }
  s_uses_capture = false;
  prompt();
}

static void fail(const char *operation, esp_err_t error) {
  finish("ERROR");
  reply("%s: %s\r\n", operation, esp_err_to_name(error));
  prompt();
}

static void begin_run(uint32_t external_hz) {
  esp_err_t error = ESP_OK;
  uint32_t configured = 0;
  s_uses_capture = s_command.kind == RFID_LAB_READ || s_command.kind == RFID_LAB_RAW ||
    s_command.kind == RFID_LAB_LISTEN;
  s_raw_count = 0;
  s_reported_id = false;
  rfid_lab_protocol_reset(&s_decoder);
  if (s_uses_capture) error = rfid_lab_io_capture(true);
  if (error == ESP_OK) error = rfid_lab_io_feedback(&s_before);
  if (error != ESP_OK) { fail("capture setup", error); return; }
  switch (s_command.kind) {
    case RFID_LAB_FIELD:
    case RFID_LAB_SWEEP:
    case RFID_LAB_READ:
    case RFID_LAB_RAW:
      error = rfid_lab_io_pwm(GPIO_RFID_LF_COIL_PIN, s_command.hz, s_command.duty, &configured);
      break;
    case RFID_LAB_PULSE:
      error = rfid_lab_io_pwm(GPIO_RFID_LF_MOD_PIN, s_command.hz, s_command.duty, &configured);
      break;
    case RFID_LAB_GPIO:
      error = rfid_lab_io_static(s_command.pin, s_command.level);
      break;
    case RFID_LAB_EMULATE:
      error = rfid_lab_io_emulate(external_hz, s_command.invert);
      break;
    default: break;
  }
  if (error != ESP_OK) { fail("start", error); return; }
  s_started = esp_timer_get_time();
  s_deadline = s_started + (int64_t)s_command.ms * US_PER_MS;
  s_report_at = s_started + REPORT_US;
  s_previous_report = s_before;
  s_phase = RFID_LAB_RUNNING;
  if (configured != 0) {
    reply("START GPIO%u requested=%" PRIu32 " Hz configured=%" PRIu32 " Hz duty=%" PRIu32 "%% ms=%" PRIu32 "\r\n",
      s_command.kind == RFID_LAB_PULSE ? GPIO_RFID_LF_MOD_PIN : GPIO_RFID_LF_COIL_PIN,
      s_command.hz, configured, s_command.duty, s_command.ms);
  } else if (s_command.kind == RFID_LAB_GPIO) {
    reply("START GPIO%" PRIu32 "=%" PRIu32 " static ms=%" PRIu32 "\r\n", s_command.pin, s_command.level, s_command.ms);
  } else {
    reply("START ms=%" PRIu32 "; off / Ctrl+C stops\r\n", s_command.ms);
  }
  if (s_command.kind == RFID_LAB_EMULATE)
    reply("EMULATE synthetic ID=0102030405 RF/64; CL=%" PRIu32 " Hz; invert=%" PRIu32 "\r\n", external_hz, s_command.invert);
  prompt();
}

static void precheck(void) {
  esp_err_t error = rfid_lab_io_off();
  if (error == ESP_OK) error = rfid_lab_io_feedback(&s_before);
  if (error != ESP_OK) { fail("precheck", error); return; }
  s_phase = RFID_LAB_PRECHECK;
  s_deadline = esp_timer_get_time() + PRECHECK_US;
}

static void poll(void) {
  int64_t now = esp_timer_get_time();
  if (s_phase == RFID_LAB_IDLE || s_phase == RFID_LAB_FAULT) return;
  if (s_phase == RFID_LAB_GAP) {
    if (now >= s_deadline) precheck();
    return;
  }
  if (s_phase == RFID_LAB_PRECHECK) {
    if (now < s_deadline) return;
    rfid_lab_feedback_t feedback;
    esp_err_t error = rfid_lab_io_feedback(&feedback);
    if (error != ESP_OK) { fail("external-field check", error); return; }
    uint32_t hz = frequency(&feedback, &s_before);
    if (s_command.kind == RFID_LAB_EMULATE) {
      if (hz < EXTERNAL_MIN_HZ || hz > EXTERNAL_MAX_HZ) {
        reply("REFUSED: external 110-140 kHz reader needed; CL=%" PRIu32 " Hz\r\n", hz);
        finish("OFF"); return;
      }
    } else if (hz >= EXTERNAL_PRESENT_HZ) {
      reply("REFUSED: external field detected (%" PRIu32 " Hz); move/stop the other reader\r\n", hz);
      finish("OFF"); return;
    }
    begin_run(hz);
    return;
  }
  drain_capture();
  if (now >= s_deadline) {
    if (s_command.kind == RFID_LAB_SWEEP && s_command.hz < s_command.end_hz) {
      rfid_lab_feedback_t feedback;
      esp_err_t error = rfid_lab_io_off();
      if (error == ESP_OK) error = rfid_lab_io_feedback(&feedback);
      if (error != ESP_OK) { fail("sweep OFF", error); return; }
      reply("STEP requested=%" PRIu32 " Hz measured_CL=%" PRIu32 " Hz DA_edges=%" PRIu32 "; OFF\r\n",
        s_command.hz, frequency(&feedback, &s_before), feedback.da_edges - s_before.da_edges);
      s_command.hz += s_command.step_hz;
      s_phase = RFID_LAB_GAP;
      s_deadline = esp_timer_get_time() + SWEEP_GAP_US;
    } else finish("DONE");
    return;
  }
  if (now >= s_report_at) {
    rfid_lab_feedback_t feedback;
    esp_err_t error = rfid_lab_io_feedback(&feedback);
    if (error != ESP_OK) { fail("feedback", error); return; }
    if (s_uses_capture && feedback.dropped != 0) { finish("CAPTURE OVERFLOW"); return; }
    reply("CL=%" PRIu32 " Hz DA_edges=%" PRIu32 " remaining=%" PRIu32 " ms\r\n",
      frequency(&feedback, &s_previous_report), feedback.da_edges - s_before.da_edges,
      (uint32_t)((s_deadline - now) / US_PER_MS));
    s_previous_report = feedback;
    s_report_at = now + REPORT_US;
    if (s_uses_capture && s_decoder.is_valid && !s_reported_id) {
      reply("DECODED EM4100 %010" PRIX64 " (3 matching frames)\r\n", s_decoder.id);
      s_reported_id = true;
    }
  }
}

static void command(char *line) {
  rfid_lab_command_t parsed;
  if (!rfid_lab_command_parse(line, &parsed)) { reply("ERROR syntax/range; type help\r\n"); prompt(); return; }
  if (parsed.kind == RFID_LAB_OFF) { finish("STOPPED"); return; }
  if (parsed.kind == RFID_LAB_STATUS) { status(); prompt(); return; }
  if (s_phase != RFID_LAB_IDLE && parsed.kind != RFID_LAB_REBOOT && parsed.kind != RFID_LAB_ROMBOOT) {
    reply("BUSY or FAULT; type off first (FAULT requires reset)\r\n"); prompt(); return;
  }
  if (parsed.kind == RFID_LAB_HELP) help();
  else if (parsed.kind == RFID_LAB_PINS) pins();
  else if (parsed.kind == RFID_LAB_INFO) {
    const esp_app_desc_t *app = esp_app_get_description();
    char hash[HEX_BYTES];
    for (size_t i = 0; i < HASH_BYTES; i++) snprintf(hash + i * HEX_DIGITS, HEX_BYTE_SIZE, "%02x", app->app_elf_sha256[i]);
    reply("INFO project=%s version=%s elf_sha256=%s idf=%s\r\n", app->project_name, app->version, hash, app->idf_ver);
  } else if (parsed.kind == RFID_LAB_REBOOT || parsed.kind == RFID_LAB_ROMBOOT) {
    finish("RESTART");
    vTaskDelay(pdMS_TO_TICKS(REBOOT_DELAY_MS));
    if (parsed.kind == RFID_LAB_ROMBOOT) REG_SET_BIT(LP_SYSTEM_REG_SYS_CTRL_REG, LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT);
    esp_restart();
  } else {
    s_command = parsed;
    if (parsed.kind == RFID_LAB_FIELD || parsed.kind == RFID_LAB_SWEEP || parsed.kind == RFID_LAB_READ ||
        parsed.kind == RFID_LAB_RAW || parsed.kind == RFID_LAB_EMULATE ||
        (parsed.kind == RFID_LAB_GPIO && parsed.pin == GPIO_RFID_LF_COIL_PIN && parsed.level != 0)) precheck();
    else begin_run(0);
    return;
  }
  prompt();
}

void rfid_lab_run(void) {
  esp_err_t error = rfid_lab_io_init();
  if (error != ESP_OK) {
    rfid_lab_io_off();
    ESP_LOGE(TAG, "RFID initialization failed: %s; no commands enabled", esp_err_to_name(error));
    return;
  }
  uart_config_t config = {.baud_rate = UART_BAUD, .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT};
  ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, UART_RX_BYTES, 0, 0, NULL, 0));
  ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &config));
  ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, GPIO_P4_UART0_TX_PIN, GPIO_P4_UART0_RX_PIN,
    UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  reply("\r\nHIGH BOY RFID LAB READY - outputs OFF; no automatic RF test\r\n");
  pins();
  reply("Type help for commands. Start example: field 125 50 5000\r\n");
  prompt();
  char line[RFID_LAB_LINE_BYTES];
  size_t length = 0;
  bool has_overflow = false;
  bool was_cr = false;
  while (true) {
    poll();
    uint8_t byte;
    int received = uart_read_bytes(UART_NUM_0, &byte, 1, pdMS_TO_TICKS(POLL_MS));
    if (received <= 0) continue;
    if (byte == ASCII_CTRL_C) {
      length = 0; has_overflow = false; finish("STOPPED"); continue;
    }
    if (byte == ASCII_CTRL_U) {
      length = 0; has_overflow = false; reply("\r\n"); prompt(); continue;
    }
    if (byte == '\r' || byte == '\n') {
      if (byte == '\n' && was_cr) { was_cr = false; continue; }
      was_cr = byte == '\r';
      reply("\r\n");
      if (has_overflow) { reply("ERROR line too long; discarded\r\n"); prompt(); }
      else if (length != 0) { line[length] = '\0'; command(line); }
      else prompt();
      length = 0; has_overflow = false;
    } else {
      was_cr = false;
      if (byte == ASCII_BACKSPACE || byte == ASCII_DELETE) {
        if (length != 0 && !has_overflow) { length--; reply("\b \b"); }
      } else if (byte >= ASCII_SPACE && byte < ASCII_DELETE) {
        if (length + 1U < sizeof(line) && !has_overflow) {
          line[length++] = byte; uart_write_bytes(UART_NUM_0, &byte, 1);
        } else has_overflow = true;
      }
    }
  }
}
