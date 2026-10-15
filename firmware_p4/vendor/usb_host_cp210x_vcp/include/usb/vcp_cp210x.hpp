/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <memory>
#include <vector>
#include "usb/cdc_acm_host.h"
#include "usb/vcp_cp210x.h"

#include "sdkconfig.h"
#ifndef CONFIG_COMPILER_CXX_EXCEPTIONS
#error This component requires C++ exceptions
#endif

namespace esp_usb {
class CP210x : public CdcAcmDevice {
public:
    /**
     * @brief Constructor for this CP210x driver
     *
     * @note USB Host library and CDC-ACM driver must be already installed
     *
     * @param[in] pid            PID eg. CP210X_PID
     * @param[in] dev_config     CDC device configuration
     * @param[in] interface_idx  Interface number
     * @return CdcAcmDevice      Pointer to created and opened CP210x device
     */
    CP210x(uint16_t pid, const cdc_acm_host_device_config_t *dev_config, uint8_t interface_idx = 0)
    {
        const esp_err_t err = cp210x_vcp_open(pid, interface_idx, dev_config, &this->cdc_hdl);
        if (err != ESP_OK) {
            throw (err);
        }
    };


    // List of supported VIDs and PIDs
    static constexpr uint16_t vid = SILICON_LABS_VID;
    static constexpr std::array<uint16_t, 3> pids = {CP210X_PID, CP2105_PID, CP2108_PID};

private:
    // Make open functions from CdcAcmDevice class private
    using CdcAcmDevice::open;
    using CdcAcmDevice::open_vendor_specific;
};
} // namespace esp_usb
