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

#include "cc1101_config.h"

#include "esp_log.h"

#define CC1101_MOD_2FSK      0
#define CC1101_MOD_ASK       2
#define CONFIG_XTAL_HZ       26000000ULL
#define CONFIG_RATE_SCALE    268435456ULL
#define CONFIG_RATE_BASE     256U
#define CONFIG_RATE_EXP_MASK 0x0FU
#define CONFIG_BW_MASK       0xF0U
#define CONFIG_BW_SHIFT      4U
#define CONFIG_BW_EXP_SHIFT  6U
#define CONFIG_BW_PART_MASK  3U
#define CONFIG_BW_DIVISOR    8U
#define CONFIG_BW_BASE       4U
#define CONFIG_DEV_SCALE     131072ULL
#define CONFIG_DEV_BASE      8U
#define CONFIG_DEV_EXP_SHIFT 4U
#define CONFIG_DEV_PART_MASK 7U
#define CONFIG_DEV_RESERVED  0x88U
#define CONFIG_LNA_SHIFT     3U
#define CONFIG_LNA_MASK      0x38U
#define CONFIG_DVGA_SHIFT    6U
#define CONFIG_DVGA_MASK     0xC0U
#define CONFIG_RATE_MIN      600U
#define CONFIG_RATE_MAX      600000U
#define CONFIG_BYTE_MAX      255U
#define CONFIG_LNA_COUNT     8U
#define CONFIG_DVGA_COUNT    4U

static const char *TAG = "CC1101_CONFIG";
static const uint32_t DATA_RATES[] = {
    600, 1200, 2400, 3794, 4798, 9600, 19200, 38400, 76800, 100000, 250000, 500000, 600000};
#define DATA_RATES_COUNT (sizeof(DATA_RATES) / sizeof(DATA_RATES[0]))

const uint8_t CC1101_PROFILE_REGISTERS[CC1101_PROFILE_REGISTER_COUNT] = {CC1101_MDMCFG4,
                                                                         CC1101_MDMCFG3,
                                                                         CC1101_MDMCFG2,
                                                                         CC1101_MDMCFG1,
                                                                         CC1101_MDMCFG0,
                                                                         CC1101_DEVIATN,
                                                                         CC1101_FIFOTHR,
                                                                         CC1101_PKTCTRL1,
                                                                         CC1101_FOCCFG,
                                                                         CC1101_AGCCTRL2,
                                                                         CC1101_AGCCTRL1,
                                                                         CC1101_AGCCTRL0,
                                                                         CC1101_FREND1,
                                                                         CC1101_WORCTRL,
                                                                         CC1101_BSCFG};

// AM270/AM650/FM238/FM476 modem values follow Flipper Zero/Momentum (GPL-3.0).
// Origin and High Boy adaptations are recorded in docs/cc1101/README.md.
static const cc1101_async_profile_t ASYNC_PROFILES[CC1101_PRESET_COUNT] = {
    [CC1101_PRESET_OOK_270KHZ] = {CC1101_MOD_ASK,
                                  {0x67,
                                   0x32,
                                   0x30,
                                   0x00,
                                   0x00,
                                   0x15,
                                   0x47,
                                   0x04,
                                   0x18,
                                   0x03,
                                   0x00,
                                   0x40,
                                   0xB6,
                                   0xFB,
                                   0x6C}},
    [CC1101_PRESET_OOK_650KHZ] = {CC1101_MOD_ASK,
                                  {0x17,
                                   0x32,
                                   0x30,
                                   0x00,
                                   0x00,
                                   0x15,
                                   0x07,
                                   0x04,
                                   0x18,
                                   0x07,
                                   0x00,
                                   0x91,
                                   0xB6,
                                   0xFB,
                                   0x6C}},
    [CC1101_PRESET_2FSK_2KHZ] = {CC1101_MOD_2FSK,
                                 {0x67,
                                  0x83,
                                  0x04,
                                  0x02,
                                  0x00,
                                  0x04,
                                  0x07,
                                  0x04,
                                  0x16,
                                  0x07,
                                  0x00,
                                  0x91,
                                  0x56,
                                  0xFB,
                                  0x6C}},
    [CC1101_PRESET_2FSK_47KHZ] = {CC1101_MOD_2FSK,
                                  {0x67,
                                   0x83,
                                   0x04,
                                   0x02,
                                   0x00,
                                   0x47,
                                   0x07,
                                   0x04,
                                   0x16,
                                   0x07,
                                   0x00,
                                   0x91,
                                   0x56,
                                   0xFB,
                                   0x6C}},
    [CC1101_PRESET_2FSK_95KHZ] = {CC1101_MOD_2FSK,
                                  {0x27,
                                   0x83,
                                   0x00,
                                   0x02,
                                   0xF8,
                                   0x57,
                                   0x07,
                                   0x00,
                                   0x16,
                                   0xC7,
                                   0x00,
                                   0x91,
                                   0x56,
                                   0xF8,
                                   0x1C}},
    [CC1101_PRESET_OOK_800KHZ] = {CC1101_MOD_ASK,
                                  {0x07,
                                   0x83,
                                   0x30,
                                   0x02,
                                   0xF8,
                                   0x47,
                                   0x07,
                                   0x00,
                                   0x14,
                                   0xC7,
                                   0x00,
                                   0x91,
                                   0x56,
                                   0xF8,
                                   0x1C}},
};

static const cc1101_async_profile_t LEGACY_FSK_PROFILE = {
    CC1101_MOD_2FSK,
    {0x07, 0x83, 0x00, 0x02, 0xF8, 0x47, 0x07, 0x00, 0x16, 0xC7, 0x00, 0x91, 0x56, 0xF8, 0x1C}};

const cc1101_async_profile_t *cc1101_config_get_profile(cc1101_preset_t preset) {
  if (preset == CC1101_PRESET_IDLE)
    return &LEGACY_FSK_PROFILE;
  if (preset < CC1101_PRESET_IDLE || preset >= CC1101_PRESET_COUNT)
    return NULL;
  return &ASYNC_PROFILES[preset];
}

esp_err_t cc1101_config_from_preset(cc1101_preset_t preset, cc1101_radio_config_t *out_config) {
  if (out_config == NULL || preset <= CC1101_PRESET_IDLE || preset >= CC1101_PRESET_COUNT)
    return ESP_ERR_INVALID_ARG;
  const cc1101_async_profile_t *profile = &ASYNC_PROFILES[preset];
  *out_config = (cc1101_radio_config_t){.preset = preset,
                                        .mdmcfg4 = profile->values[CC1101_PROFILE_MDMCFG4],
                                        .mdmcfg3 = profile->values[CC1101_PROFILE_MDMCFG3],
                                        .deviatn = profile->values[CC1101_PROFILE_DEVIATN],
                                        .agcctrl2 = profile->values[CC1101_PROFILE_AGCCTRL2]};
  return ESP_OK;
}

bool cc1101_config_is_fsk(const cc1101_radio_config_t *config) {
  return config != NULL && config->preset >= CC1101_PRESET_2FSK_2KHZ &&
         config->preset <= CC1101_PRESET_2FSK_95KHZ;
}

uint32_t cc1101_config_value(const cc1101_radio_config_t *config, cc1101_config_field_t field) {
  if (config == NULL)
    return 0;
  switch (field) {
    case CC1101_CONFIG_BANDWIDTH:
      return CONFIG_XTAL_HZ /
             (CONFIG_BW_DIVISOR *
              (CONFIG_BW_BASE + ((config->mdmcfg4 >> CONFIG_BW_SHIFT) & CONFIG_BW_PART_MASK)) *
              (1U << (config->mdmcfg4 >> CONFIG_BW_EXP_SHIFT)));
    case CC1101_CONFIG_DATA_RATE:
      return ((CONFIG_RATE_BASE + config->mdmcfg3) * CONFIG_XTAL_HZ *
                  (1U << (config->mdmcfg4 & CONFIG_RATE_EXP_MASK)) +
              CONFIG_RATE_SCALE / 2) /
             CONFIG_RATE_SCALE;
    case CC1101_CONFIG_DEVIATION:
      return ((CONFIG_DEV_BASE + (config->deviatn & CONFIG_DEV_PART_MASK)) * CONFIG_XTAL_HZ *
                  (1U << ((config->deviatn >> CONFIG_DEV_EXP_SHIFT) & CONFIG_DEV_PART_MASK)) +
              CONFIG_DEV_SCALE / 2) /
             CONFIG_DEV_SCALE;
    case CC1101_CONFIG_LNA_GAIN:
      return (config->agcctrl2 & CONFIG_LNA_MASK) >> CONFIG_LNA_SHIFT;
    case CC1101_CONFIG_DVGA_GAIN:
      return (config->agcctrl2 & CONFIG_DVGA_MASK) >> CONFIG_DVGA_SHIFT;
    default:
      return 0;
  }
}

esp_err_t cc1101_config_validate(const cc1101_radio_config_t *config) {
  if (config == NULL || config->preset <= CC1101_PRESET_IDLE ||
      config->preset >= CC1101_PRESET_COUNT || (config->deviatn & CONFIG_DEV_RESERVED) != 0)
    return ESP_ERR_INVALID_ARG;
  uint32_t rate = cc1101_config_value(config, CC1101_CONFIG_DATA_RATE);
  return rate >= CONFIG_RATE_MIN && rate <= CONFIG_RATE_MAX ? ESP_OK : ESP_ERR_INVALID_ARG;
}

bool cc1101_config_is_custom(const cc1101_radio_config_t *config) {
  cc1101_radio_config_t base;
  if (config == NULL || cc1101_config_from_preset(config->preset, &base) != ESP_OK)
    return false;
  return config->mdmcfg4 != base.mdmcfg4 || config->mdmcfg3 != base.mdmcfg3 ||
         config->deviatn != base.deviatn || config->agcctrl2 != base.agcctrl2;
}

esp_err_t
cc1101_config_step(cc1101_radio_config_t *config, cc1101_config_field_t field, int direction) {
  if (cc1101_config_validate(config) != ESP_OK || field < 0 || field >= CC1101_CONFIG_COUNT ||
      (direction != -1 && direction != 1))
    return ESP_ERR_INVALID_ARG;
  switch (field) {
    case CC1101_CONFIG_BANDWIDTH: {
      int value = (config->mdmcfg4 >> CONFIG_BW_SHIFT) - direction;
      value = (value + CONFIG_RATE_EXP_MASK + 1) & CONFIG_RATE_EXP_MASK;
      config->mdmcfg4 = (config->mdmcfg4 & CONFIG_RATE_EXP_MASK) | (value << CONFIG_BW_SHIFT);
      break;
    }
    case CC1101_CONFIG_DATA_RATE: {
      uint32_t current = cc1101_config_value(config, field);
      size_t nearest = 0;
      uint32_t nearest_error = UINT32_MAX;
      for (size_t i = 0; i < DATA_RATES_COUNT; ++i) {
        uint32_t difference =
            current > DATA_RATES[i] ? current - DATA_RATES[i] : DATA_RATES[i] - current;
        if (difference < nearest_error) {
          nearest = i;
          nearest_error = difference;
        }
      }
      size_t next = (nearest + DATA_RATES_COUNT + direction) % DATA_RATES_COUNT;
      uint32_t target = DATA_RATES[next];
      uint32_t best_error = UINT32_MAX;
      cc1101_radio_config_t candidate = *config, best = *config;
      for (uint32_t exponent = 0; exponent <= CONFIG_RATE_EXP_MASK; ++exponent) {
        for (uint32_t mantissa = 0; mantissa <= CONFIG_BYTE_MAX; ++mantissa) {
          candidate.mdmcfg4 = (config->mdmcfg4 & CONFIG_BW_MASK) | exponent;
          candidate.mdmcfg3 = mantissa;
          uint32_t rate = cc1101_config_value(&candidate, field);
          uint32_t error = rate > target ? rate - target : target - rate;
          if (rate >= CONFIG_RATE_MIN && rate <= CONFIG_RATE_MAX && error < best_error) {
            best = candidate;
            best_error = error;
          }
        }
      }
      *config = best;
      break;
    }
    case CC1101_CONFIG_DEVIATION: {
      if (!cc1101_config_is_fsk(config))
        return ESP_ERR_NOT_SUPPORTED;
      int value = ((config->deviatn >> CONFIG_DEV_EXP_SHIFT) * CONFIG_DEV_BASE) +
                  (config->deviatn & CONFIG_DEV_PART_MASK);
      value = (value + direction + CONFIG_DEV_BASE * CONFIG_DEV_BASE) %
              (CONFIG_DEV_BASE * CONFIG_DEV_BASE);
      config->deviatn =
          ((value / CONFIG_DEV_BASE) << CONFIG_DEV_EXP_SHIFT) | (value % CONFIG_DEV_BASE);
      break;
    }
    case CC1101_CONFIG_LNA_GAIN: {
      uint8_t value =
          (cc1101_config_value(config, field) + CONFIG_LNA_COUNT + direction) % CONFIG_LNA_COUNT;
      config->agcctrl2 = (config->agcctrl2 & ~CONFIG_LNA_MASK) | (value << CONFIG_LNA_SHIFT);
      break;
    }
    case CC1101_CONFIG_DVGA_GAIN: {
      uint8_t value =
          (cc1101_config_value(config, field) + CONFIG_DVGA_COUNT + direction) % CONFIG_DVGA_COUNT;
      config->agcctrl2 = (config->agcctrl2 & ~CONFIG_DVGA_MASK) | (value << CONFIG_DVGA_SHIFT);
      break;
    }
    default:
      ESP_LOGE(TAG, "Invalid configuration field");
      return ESP_ERR_INVALID_ARG;
  }
  return ESP_OK;
}
