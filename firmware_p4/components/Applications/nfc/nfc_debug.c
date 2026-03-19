// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "nfc_debug.h"
#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "st25r3916_aat.h"
#include "hb_nfc_spi.h"

hb_nfc_err_t nfc_debug_cw_on(void)
{
    return st25r_field_on();
}

void nfc_debug_cw_off(void)
{
    st25r_field_off();
}

void nfc_debug_dump_regs(void)
{
    st25r_dump_regs();
}

hb_nfc_err_t nfc_debug_aat_sweep(void)
{
    st25r_aat_result_t result;
    return st25r_aat_calibrate(&result);
}
