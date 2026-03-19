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
