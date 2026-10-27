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

#include "nfc_apdu.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "nfc_tcl.h"

static const char *TAG = "NFC_APDU";

#define APDU_MAX_BUF                  NFC_APDU_MAX_COMMAND
#define APDU_ROUND_BUF                1024U
#define APDU_STATUS_WORD_SIZE         2U    /**< Status word is always 2 bytes */
#define APDU_MIN_RESPONSE_SIZE        2U    /**< Minimum response is just SW */
#define APDU_SELECT_AID_P2            0x04U /**< P2: select by name (AID) */
#define APDU_SELECT_FILE_P1           0x00U /**< P1: select by file ID */
#define APDU_SELECT_FILE_P2           0x0CU /**< P2: select by file ID */
#define APDU_SELECT_FILE_LC           0x02U /**< Lc for FILE ID (2 bytes) */
#define APDU_SELECT_FILE_MIN_SIZE     7U    /**< Minimum APDU size for SELECT FILE */
#define APDU_SELECT_FILE_SIZE_WITH_LE 8U    /**< SELECT FILE APDU size with Le */
#define APDU_BYTE_HIGH_SHIFT          8U    /**< Shift for high byte in 16-bit value */
#define APDU_BYTE_MASK                0xFFU /**< Mask for single byte */
#define APDU_LE_UNSPECIFIED           0x00U /**< Le = 0 means return all available */

static uint16_t apdu_status_from_response(const uint8_t *buf, size_t len) {
  return (uint16_t)(((uint16_t)buf[len - 2U] << 8U) | buf[len - 1U]);
}

static bool apdu_more_data(uint16_t sw, const nfc_apdu_options_t *options) {
  const uint16_t sw_class = sw & APDU_SW_CLASS_MASK;
  return sw_class == APDU_SW_MORE_DATA || (options->accept_9f_more && sw_class == 0x9F00U);
}

static size_t apdu_copy_response_data(nfc_apdu_resp_t *resp,
                                      size_t out_pos,
                                      const uint8_t *data,
                                      size_t data_len) {
  if (data_len == 0)
    return out_pos;
  const size_t room = out_pos < resp->max ? resp->max - out_pos : 0U;
  const size_t copy = data_len < room ? data_len : room;
  if (copy > 0)
    memcpy(&resp->buf[out_pos], data, copy);
  if (copy != data_len)
    resp->truncated = true;
  return out_pos + copy;
}

static bool apdu_set_corrected_le(uint8_t *cmd, size_t *cmd_len, size_t cmd_max, uint8_t sw2) {
  nfc_apdu_cmd_info_t info;
  if (!nfc_apdu_parse_command(cmd, *cmd_len, &info))
    return false;

  if (!info.le_present) {
    if (info.extended) {
      if (*cmd_len + 2U > cmd_max)
        return false;
      cmd[(*cmd_len)++] = 0x00U;
      cmd[(*cmd_len)++] = sw2;
    } else {
      if (*cmd_len + 1U > cmd_max)
        return false;
      cmd[(*cmd_len)++] = sw2;
    }
    return true;
  }

  if (info.le_size == 1U) {
    cmd[info.le_offset] = sw2;
  } else if (info.le_size == 2U) {
    const uint16_t corrected = sw2 == 0U ? 256U : sw2;
    cmd[info.le_offset] = (uint8_t)(corrected >> 8U);
    cmd[info.le_offset + 1U] = (uint8_t)corrected;
  } else {
    return false;
  }
  return true;
}

const char *nfc_apdu_sw_str(uint16_t sw) {
  switch (sw) {
    case APDU_SW_OK:
      return "OK";
    case APDU_SW_WRONG_LENGTH:
      return "Wrong length";
    case APDU_SW_SECURITY:
      return "Security status not satisfied";
    case APDU_SW_AUTH_FAILED:
      return "Authentication failed";
    case APDU_SW_FILE_NOT_FOUND:
      return "File not found";
    case APDU_SW_INS_NOT_SUP:
      return "INS not supported";
    case APDU_SW_CLA_NOT_SUP:
      return "CLA not supported";
    case APDU_SW_UNKNOWN:
      return "Unknown error";
    default:
      return "SW error";
  }
}

void nfc_apdu_sw_format(uint16_t sw, char *out, size_t out_size) {
  if (out == NULL || out_size == 0U)
    return;
  const uint8_t sw1 = (uint8_t)(sw >> 8U);
  const uint8_t sw2 = (uint8_t)sw;
  const char *text = NULL;
  switch (sw) {
    case 0x9000U:
      text = "Success";
      break;
    case 0x6200U:
      text = "Warning: state unchanged";
      break;
    case 0x6282U:
      text = "Warning: end of file reached";
      break;
    case 0x6300U:
      text = "Authentication or verification failed";
      break;
    case 0x6581U:
      text = "Memory failure";
      break;
    case 0x6700U:
      text = "Wrong command length";
      break;
    case 0x6881U:
      text = "Logical channel not supported";
      break;
    case 0x6882U:
      text = "Secure messaging not supported";
      break;
    case 0x6982U:
      text = "Security status not satisfied";
      break;
    case 0x6985U:
      text = "Conditions of use not satisfied";
      break;
    case 0x6A80U:
      text = "Incorrect data field";
      break;
    case 0x6A81U:
      text = "Function not supported";
      break;
    case 0x6A82U:
      text = "File or application not found";
      break;
    case 0x6A83U:
      text = "Record not found";
      break;
    case 0x6A84U:
      text = "Not enough memory";
      break;
    case 0x6A86U:
      text = "Incorrect P1/P2";
      break;
    case 0x6B00U:
      text = "Wrong offset";
      break;
    case 0x6D00U:
      text = "Instruction not supported";
      break;
    case 0x6E00U:
      text = "Class not supported";
      break;
    case 0x6F00U:
      text = "No precise diagnosis";
      break;
    default:
      break;
  }
  if (text != NULL) {
    snprintf(out, out_size, "%04X  %s", sw, text);
  } else if (sw1 == 0x61U || sw1 == 0x9FU) {
    snprintf(out, out_size, "%04X  %u response bytes available", sw, sw2 != 0U ? sw2 : 256U);
  } else if (sw1 == 0x63U && (sw2 & 0xF0U) == 0xC0U) {
    snprintf(out, out_size, "%04X  Verification failed; %u retries remain", sw, sw2 & 0x0FU);
  } else if (sw1 == 0x6CU) {
    snprintf(out, out_size, "%04X  Correct Le is %u", sw, sw2 != 0U ? sw2 : 256U);
  } else if (sw1 == 0x62U || sw1 == 0x63U) {
    snprintf(out, out_size, "%04X  Warning status", sw);
  } else {
    snprintf(out, out_size, "%04X  Card-specific status", sw);
  }
}

const char *nfc_apdu_ins_name(uint8_t cla, uint8_t ins) {
  (void)cla;
  switch (ins) {
    case 0x04U:
      return "DEACTIVATE FILE";
    case 0x20U:
      return "VERIFY";
    case 0x24U:
      return "CHANGE REFERENCE DATA";
    case 0x2CU:
      return "RESET RETRY COUNTER";
    case 0x44U:
      return "ACTIVATE FILE";
    case 0x70U:
      return "MANAGE CHANNEL";
    case 0x82U:
      return "EXTERNAL AUTHENTICATE";
    case 0x84U:
      return "GET CHALLENGE";
    case 0x86U:
      return "GENERAL AUTHENTICATE";
    case 0x88U:
      return "INTERNAL AUTHENTICATE";
    case 0xA4U:
      return "SELECT";
    case 0xA8U:
      return "GET PROCESSING OPTIONS";
    case 0xAEU:
      return "GENERATE AC";
    case 0xB0U:
      return "READ BINARY";
    case 0xB2U:
      return "READ RECORD";
    case 0xC0U:
      return "GET RESPONSE";
    case 0xCAU:
      return "GET DATA";
    case 0xD0U:
      return "WRITE BINARY";
    case 0xD2U:
      return "WRITE RECORD";
    case 0xD6U:
      return "UPDATE BINARY";
    case 0xDAU:
      return "PUT DATA";
    case 0xDCU:
      return "UPDATE RECORD";
    case 0xE0U:
      return "CREATE FILE";
    case 0xE2U:
      return "APPEND RECORD";
    case 0xE4U:
      return "DELETE FILE";
    default:
      return "PROPRIETARY / UNKNOWN";
  }
}

nfc_apdu_risk_t nfc_apdu_classify(const nfc_apdu_cmd_info_t *cmd) {
  if (cmd == NULL)
    return NFC_APDU_RISK_UNKNOWN;
  switch (cmd->ins) {
    case 0xA4U:
    case 0xB0U:
    case 0xB2U:
    case 0xC0U:
    case 0xCAU:
    case 0x84U:
      return NFC_APDU_RISK_READ_ONLY;
    case 0x20U:
    case 0x82U:
    case 0x86U:
    case 0x88U:
      return NFC_APDU_RISK_AUTHENTICATION;
    case 0xAEU:
      return NFC_APDU_RISK_PAYMENT;
    case 0x04U:
    case 0x24U:
    case 0x26U:
    case 0x2CU:
    case 0x44U:
    case 0x46U:
    case 0xD0U:
    case 0xD2U:
    case 0xD6U:
    case 0xDAU:
    case 0xDBU:
    case 0xDCU:
    case 0xE0U:
    case 0xE2U:
    case 0xE4U:
    case 0xE6U:
      return NFC_APDU_RISK_MUTATING;
    case 0xA8U:
      return (cmd->cla & 0x80U) != 0U ? NFC_APDU_RISK_PAYMENT : NFC_APDU_RISK_UNKNOWN;
    default:
      return NFC_APDU_RISK_UNKNOWN;
  }
}

const char *nfc_apdu_risk_name(nfc_apdu_risk_t risk) {
  switch (risk) {
    case NFC_APDU_RISK_READ_ONLY:
      return "READ-ONLY";
    case NFC_APDU_RISK_AUTHENTICATION:
      return "AUTH / RETRY RISK";
    case NFC_APDU_RISK_MUTATING:
      return "MUTATING";
    case NFC_APDU_RISK_PAYMENT:
      return "PAYMENT STATE";
    default:
      return "UNKNOWN";
  }
}

bool nfc_apdu_parse_command(const uint8_t *apdu, size_t apdu_len, nfc_apdu_cmd_info_t *out) {
  if (apdu == NULL || out == NULL || apdu_len < 4U)
    return false;
  memset(out, 0, sizeof(*out));
  out->cla = apdu[0];
  out->ins = apdu[1];
  out->p1 = apdu[2];
  out->p2 = apdu[3];
  if (apdu_len == 4U) {
    out->apdu_case = NFC_APDU_CASE_1;
    return true;
  }

  const uint8_t b1 = apdu[4];
  if (apdu_len == 5U) {
    out->apdu_case = NFC_APDU_CASE_2_SHORT;
    out->le_present = true;
    out->le = b1 != 0U ? b1 : 256U;
    out->le_offset = 4U;
    out->le_size = 1U;
    return true;
  }

  if (b1 != 0U) {
    const size_t lc = b1;
    if (apdu_len != 5U + lc && apdu_len != 6U + lc)
      return false;
    out->lc = lc;
    out->data = &apdu[5];
    if (apdu_len == 5U + lc) {
      out->apdu_case = NFC_APDU_CASE_3_SHORT;
    } else {
      out->apdu_case = NFC_APDU_CASE_4_SHORT;
      out->le_present = true;
      out->le_offset = 5U + lc;
      out->le_size = 1U;
      out->le = apdu[out->le_offset] != 0U ? apdu[out->le_offset] : 256U;
    }
    return true;
  }

  if (apdu_len < 7U)
    return false;
  out->extended = true;
  const uint16_t ext = (uint16_t)(((uint16_t)apdu[5] << 8U) | apdu[6]);
  if (apdu_len == 7U) {
    out->apdu_case = NFC_APDU_CASE_2_EXTENDED;
    out->le_present = true;
    out->le = ext != 0U ? ext : 65536U;
    out->le_offset = 5U;
    out->le_size = 2U;
    return true;
  }
  if (ext == 0U)
    return false;
  const size_t lc = ext;
  if (apdu_len != 7U + lc && apdu_len != 9U + lc)
    return false;
  out->lc = lc;
  out->data = &apdu[7];
  if (apdu_len == 7U + lc) {
    out->apdu_case = NFC_APDU_CASE_3_EXTENDED;
  } else {
    out->apdu_case = NFC_APDU_CASE_4_EXTENDED;
    out->le_present = true;
    out->le_offset = 7U + lc;
    out->le_size = 2U;
    const uint16_t le =
        (uint16_t)(((uint16_t)apdu[out->le_offset] << 8U) | apdu[out->le_offset + 1U]);
    out->le = le != 0U ? le : 65536U;
  }
  return true;
}

size_t nfc_apdu_build_command(nfc_apdu_buf_t dst, const nfc_apdu_command_t *cmd) {
  if (dst.buf == NULL || cmd == NULL || (cmd->lc > 0U && cmd->data == NULL) || cmd->lc > 65535U ||
      (cmd->le_present && (cmd->le == 0U || cmd->le > 65536U)))
    return 0;
  const bool extended =
      cmd->force_extended || cmd->lc > 255U || (cmd->le_present && cmd->le > 256U);
  const size_t needed = 4U + (cmd->lc > 0U ? (extended ? 3U : 1U) + cmd->lc : 0U) +
                        (cmd->le_present ? (extended ? (cmd->lc > 0U ? 2U : 3U) : 1U) : 0U);
  if (needed > dst.max)
    return 0;
  size_t pos = 0;
  dst.buf[pos++] = cmd->cla;
  dst.buf[pos++] = cmd->ins;
  dst.buf[pos++] = cmd->p1;
  dst.buf[pos++] = cmd->p2;
  if (cmd->lc > 0U) {
    if (extended) {
      dst.buf[pos++] = 0x00U;
      dst.buf[pos++] = (uint8_t)(cmd->lc >> 8U);
      dst.buf[pos++] = (uint8_t)cmd->lc;
    } else {
      dst.buf[pos++] = (uint8_t)cmd->lc;
    }
    memcpy(&dst.buf[pos], cmd->data, cmd->lc);
    pos += cmd->lc;
  }
  if (cmd->le_present) {
    if (extended) {
      if (cmd->lc == 0U)
        dst.buf[pos++] = 0x00U;
      const uint32_t le = cmd->le == 65536U ? 0U : cmd->le;
      dst.buf[pos++] = (uint8_t)(le >> 8U);
      dst.buf[pos++] = (uint8_t)le;
    } else {
      dst.buf[pos++] = cmd->le == 256U ? 0x00U : (uint8_t)cmd->le;
    }
  }
  return pos;
}

bool nfc_apdu_hex_decode(
    const char *text, uint8_t *out, size_t out_max, size_t *out_len, size_t *error_pos) {
  if (out_len != NULL)
    *out_len = 0;
  if (error_pos != NULL)
    *error_pos = 0;
  if (text == NULL || out == NULL)
    return false;
  size_t count = 0;
  int high = -1;
  for (size_t i = 0; text[i] != '\0'; i++) {
    const unsigned char c = (unsigned char)text[i];
    if (isspace(c) || c == ':' || c == '-' || c == '_')
      continue;
    if (!isxdigit(c)) {
      if (error_pos != NULL)
        *error_pos = i;
      return false;
    }
    const int value = isdigit(c) ? c - '0' : (toupper(c) - 'A' + 10);
    if (high < 0) {
      high = value;
    } else {
      if (count >= out_max) {
        if (error_pos != NULL)
          *error_pos = i;
        return false;
      }
      out[count++] = (uint8_t)((high << 4) | value);
      high = -1;
    }
  }
  if (high >= 0) {
    if (error_pos != NULL)
      *error_pos = strlen(text);
    return false;
  }
  if (out_len != NULL)
    *out_len = count;
  return count > 0U;
}

size_t nfc_apdu_hex_encode(
    const uint8_t *data, size_t data_len, char separator, char *out, size_t out_size) {
  static const char hex[] = "0123456789ABCDEF";
  if (out == NULL || out_size == 0U || (data == NULL && data_len != 0U))
    return 0;
  size_t pos = 0;
  for (size_t i = 0; i < data_len; i++) {
    if (i > 0U && separator != '\0') {
      if (pos + 1U >= out_size)
        break;
      out[pos++] = separator;
    }
    if (pos + 2U >= out_size)
      break;
    out[pos++] = hex[data[i] >> 4U];
    out[pos++] = hex[data[i] & 0x0FU];
  }
  out[pos] = '\0';
  return pos;
}

hb_nfc_err_t nfc_apdu_sw_to_err(uint16_t sw) {
  if (sw == APDU_SW_OK)
    return HB_NFC_OK;
  if (sw == APDU_SW_SECURITY || sw == APDU_SW_AUTH_FAILED)
    return HB_NFC_ERR_AUTH;
  return HB_NFC_ERR_PROTOCOL;
}

static int apdu_transceive_raw(const nfc_apdu_channel_t *ch,
                               const uint8_t *apdu,
                               size_t apdu_len,
                               uint8_t *rx,
                               size_t rx_max) {
  return nfc_tcl_transceive(ch->proto, ch->ctx, apdu, apdu_len, rx, rx_max, ch->timeout_ms);
}

hb_nfc_err_t nfc_apdu_transceive(const nfc_apdu_channel_t *ch,
                                 const uint8_t *apdu,
                                 size_t apdu_len,
                                 nfc_apdu_resp_t *resp) {
  const nfc_apdu_options_t options = NFC_APDU_OPTIONS_DEFAULT();
  return nfc_apdu_transceive_ex(ch, apdu, apdu_len, &options, resp);
}

hb_nfc_err_t nfc_apdu_transceive_ex(const nfc_apdu_channel_t *ch,
                                    const uint8_t *apdu,
                                    size_t apdu_len,
                                    const nfc_apdu_options_t *options,
                                    nfc_apdu_resp_t *resp) {
  if (!ch || !ch->ctx || !apdu || apdu_len == 0 || !resp || !resp->buf ||
      resp->max < APDU_STATUS_WORD_SIZE)
    return HB_NFC_ERR_PARAM;

  nfc_apdu_cmd_info_t parsed;
  if (!nfc_apdu_parse_command(apdu, apdu_len, &parsed))
    return HB_NFC_ERR_PARAM;

  uint8_t cmd[APDU_MAX_BUF];
  if (apdu_len > sizeof(cmd))
    return HB_NFC_ERR_PARAM;
  memcpy(cmd, apdu, apdu_len);
  size_t cmd_len = apdu_len;
  const nfc_apdu_options_t default_options = NFC_APDU_OPTIONS_DEFAULT();
  const nfc_apdu_options_t *opts = options != NULL ? options : &default_options;
  const uint8_t max_exchanges = opts->max_exchanges != 0U ? opts->max_exchanges : 1U;
  const int64_t started = esp_timer_get_time();
  const uint8_t original_cla = apdu[0];
  size_t out_pos = 0;
  uint16_t status = 0;
  hb_nfc_err_t result = HB_NFC_ERR_PROTOCOL;
  memset(resp->buf, 0, resp->max);
  resp->len = 0;
  resp->sw = 0;
  resp->truncated = false;
  resp->le_corrected = false;
  resp->exchanges = 0;
  resp->elapsed_us = 0;

  while (resp->exchanges < max_exchanges) {
    uint8_t round[APDU_ROUND_BUF];
    const int len = apdu_transceive_raw(ch, cmd, cmd_len, round, sizeof(round));
    resp->exchanges++;
    if (len < (int)APDU_MIN_RESPONSE_SIZE) {
      /* No received frame is a transport timeout, not an ISO 7816 status.
       * Keep HB_NFC_ERR_PROTOCOL for a malformed one-byte response only. */
      result = len == 0 ? HB_NFC_ERR_TIMEOUT : HB_NFC_ERR_PROTOCOL;
      goto done;
    }
    status = apdu_status_from_response(round, (size_t)len);

    if ((status & APDU_SW_CLASS_MASK) == APDU_SW_WRONG_LE && opts->auto_correct_le &&
        !resp->le_corrected) {
      if (!apdu_set_corrected_le(
              cmd, &cmd_len, sizeof(cmd), (uint8_t)(status & APDU_SW_DATA_MASK))) {
        result = HB_NFC_ERR_PROTOCOL;
        goto done;
      }
      resp->le_corrected = true;
      continue;
    }

    out_pos = apdu_copy_response_data(resp, out_pos, round, (size_t)len - APDU_STATUS_WORD_SIZE);
    if (apdu_more_data(status, opts) && opts->auto_get_response) {
      const uint8_t le = (uint8_t)(status & APDU_SW_DATA_MASK);
      cmd[0] = original_cla;
      cmd[1] = APDU_INS_GET_RESPONSE;
      cmd[2] = 0x00U;
      cmd[3] = 0x00U;
      cmd[4] = le != 0U ? le : APDU_LE_UNSPECIFIED;
      cmd_len = APDU_HEADER_SIZE;
      continue;
    }

    result = nfc_apdu_sw_to_err(status);
    goto done;
  }

  result = HB_NFC_ERR_PROTOCOL;

done:
  resp->sw = status;
  resp->len = out_pos;
  const int64_t elapsed = esp_timer_get_time() - started;
  resp->elapsed_us = elapsed > 0 ? (uint32_t)elapsed : 0U;
  ESP_LOGD(TAG,
           "APDU %s CLA=%02X INS=%02X exchanges=%u SW=%04X len=%u time=%uus%s",
           nfc_apdu_ins_name(parsed.cla, parsed.ins),
           parsed.cla,
           parsed.ins,
           resp->exchanges,
           resp->sw,
           (unsigned)resp->len,
           (unsigned)resp->elapsed_us,
           resp->truncated ? " truncated" : "");
  return result;
}

hb_nfc_err_t nfc_apdu_send(const nfc_apdu_channel_t *ch,
                           const uint8_t *apdu,
                           size_t apdu_len,
                           nfc_apdu_resp_t *resp) {
  return nfc_apdu_transceive(ch, apdu, apdu_len, resp);
}

hb_nfc_err_t nfc_apdu_recv(const nfc_apdu_channel_t *ch,
                           const uint8_t *apdu,
                           size_t apdu_len,
                           nfc_apdu_resp_t *resp) {
  return nfc_apdu_transceive(ch, apdu, apdu_len, resp);
}

size_t
nfc_apdu_build_select_aid(nfc_apdu_buf_t dst, nfc_apdu_data_t aid, bool le_present, uint8_t le) {
  if (!dst.buf || !aid.ptr || aid.len == 0)
    return 0;
  if (dst.max < APDU_HEADER_SIZE + aid.len + (le_present ? 1U : 0U))
    return 0;
  size_t pos = 0;
  dst.buf[pos++] = APDU_CLA_ISO7816;
  dst.buf[pos++] = APDU_INS_SELECT;
  dst.buf[pos++] = APDU_SELECT_AID_P2;
  dst.buf[pos++] = APDU_SELECT_FILE_P1;
  dst.buf[pos++] = (uint8_t)aid.len;
  memcpy(&dst.buf[pos], aid.ptr, aid.len);
  pos += aid.len;
  if (le_present)
    dst.buf[pos++] = le;
  return pos;
}

size_t nfc_apdu_build_select_file(nfc_apdu_buf_t dst, uint16_t fid, bool le_present, uint8_t le) {
  if (!dst.buf || dst.max < (APDU_SELECT_FILE_MIN_SIZE + (le_present ? 1U : 0U)))
    return 0;
  size_t pos = 0;
  dst.buf[pos++] = APDU_CLA_ISO7816;
  dst.buf[pos++] = APDU_INS_SELECT;
  dst.buf[pos++] = APDU_SELECT_FILE_P1;
  dst.buf[pos++] = APDU_SELECT_FILE_P2;
  dst.buf[pos++] = APDU_SELECT_FILE_LC;
  dst.buf[pos++] = (uint8_t)((fid >> APDU_BYTE_HIGH_SHIFT) & APDU_BYTE_MASK);
  dst.buf[pos++] = (uint8_t)(fid & APDU_BYTE_MASK);
  if (le_present)
    dst.buf[pos++] = le;
  return pos;
}

size_t nfc_apdu_build_read_binary(nfc_apdu_buf_t dst, uint16_t offset, uint8_t le) {
  if (!dst.buf || dst.max < APDU_HEADER_SIZE)
    return 0;
  dst.buf[0] = APDU_CLA_ISO7816;
  dst.buf[1] = APDU_INS_READ_BINARY;
  dst.buf[2] = (uint8_t)((offset >> 8) & 0xFFU);
  dst.buf[3] = (uint8_t)(offset & 0xFFU);
  dst.buf[4] = le;
  return APDU_HEADER_SIZE;
}

size_t nfc_apdu_build_update_binary(nfc_apdu_buf_t dst, uint16_t offset, nfc_apdu_data_t data) {
  if (!dst.buf || !data.ptr || data.len == 0 || data.len > APDU_LE_MAX)
    return 0;
  if (dst.max < APDU_HEADER_SIZE + data.len)
    return 0;
  size_t pos = 0;
  dst.buf[pos++] = APDU_CLA_ISO7816;
  dst.buf[pos++] = APDU_INS_UPDATE_BINARY;
  dst.buf[pos++] = (uint8_t)((offset >> 8) & 0xFFU);
  dst.buf[pos++] = (uint8_t)(offset & 0xFFU);
  dst.buf[pos++] = (uint8_t)data.len;
  memcpy(&dst.buf[pos], data.ptr, data.len);
  pos += data.len;
  return pos;
}

size_t nfc_apdu_build_verify(nfc_apdu_buf_t dst, uint8_t p1, uint8_t p2, nfc_apdu_data_t data) {
  if (!dst.buf || !data.ptr || data.len == 0 || data.len > APDU_LE_MAX)
    return 0;
  if (dst.max < APDU_HEADER_SIZE + data.len)
    return 0;
  size_t pos = 0;
  dst.buf[pos++] = APDU_CLA_ISO7816;
  dst.buf[pos++] = APDU_INS_VERIFY;
  dst.buf[pos++] = p1;
  dst.buf[pos++] = p2;
  dst.buf[pos++] = (uint8_t)data.len;
  memcpy(&dst.buf[pos], data.ptr, data.len);
  pos += data.len;
  return pos;
}

size_t nfc_apdu_build_get_data(nfc_apdu_buf_t dst, uint8_t p1, uint8_t p2, uint8_t le) {
  if (!dst.buf || dst.max < APDU_HEADER_SIZE)
    return 0;
  dst.buf[0] = APDU_CLA_ISO7816;
  dst.buf[1] = APDU_INS_GET_DATA;
  dst.buf[2] = p1;
  dst.buf[3] = p2;
  dst.buf[4] = le;
  return APDU_HEADER_SIZE;
}

bool nfc_apdu_parse_fci(const uint8_t *fci, size_t fci_len, nfc_fci_info_t *out) {
  if (!fci || fci_len < 2U || !out)
    return false;
  memset(out, 0, sizeof(*out));

  size_t pos = 0;
  while (pos + 2U <= fci_len) {
    uint8_t tag = fci[pos++];
    uint8_t len = fci[pos++];
    if (pos + len > fci_len)
      break;

    if (tag == APDU_FCI_TAG_DF_NAME) {
      size_t copy = (len > sizeof(out->df_name)) ? sizeof(out->df_name) : len;
      memcpy(out->df_name, &fci[pos], copy);
      out->df_name_len = (uint8_t)copy;
      return true;
    }

    pos += len;
  }
  return false;
}
