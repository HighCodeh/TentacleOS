/**
 * @file llcp.c
 * @brief LLCP (Logical Link Control Protocol) over NFC-DEP (best-effort).
 */
#include "llcp.h"

#include <string.h>
#include "esp_log.h"
#include "esp_random.h"

#include "nfc_poller.h"
#include "nfc_rf.h"
#include "nfc_common.h"
#include "iso14443a.h"
#include "st25r3916_core.h"

#define TAG "llcp"

#define NFCDEP_CMD0_REQ      0xD4U
#define NFCDEP_CMD0_RES      0xD5U
#define NFCDEP_CMD1_ATR_REQ  0x00U
#define NFCDEP_CMD1_ATR_RES  0x01U
#define NFCDEP_CMD1_DEP_REQ  0x06U
#define NFCDEP_CMD1_DEP_RES  0x07U

#define NFCDEP_PFB_PNI_MASK  0x03U
#define NFCDEP_PFB_DID       0x04U
#define NFCDEP_PFB_NAD       0x08U
#define NFCDEP_PFB_MI        0x10U

static int llcp_strip_crc(uint8_t* buf, int len)
{
    if (len >= 3 && iso14443a_check_crc(buf, (size_t)len)) return len - 2;
    return len;
}

static uint16_t llcp_rwt_ms(uint8_t to)
{
    uint32_t us = 302U * (1U << (to & 0x0FU));
    uint32_t ms = (us + 999U) / 1000U;
    if (ms < 5U) ms = 5U;
    return (uint16_t)ms;
}

static uint16_t llcp_lr_from_pp(uint8_t pp)
{
    if ((pp & 0x40U) == 0) return 64U;
    switch ((pp >> 4) & 0x03U) {
    case 0: return 64U;
    case 1: return 128U;
    case 2: return 192U;
    default: return 254U;
    }
}

static uint8_t llcp_pp_from_miu(uint16_t miu)
{
    uint8_t lr_bits = 0;
    if (miu > 192U) lr_bits = 3;
    else if (miu > 128U) lr_bits = 2;
    else if (miu > 64U)  lr_bits = 1;
    else lr_bits = 0;
    return (uint8_t)(0x40U | (lr_bits << 4));
}

void llcp_params_default(llcp_params_t* params)
{
    if (!params) return;
    params->version_major = 1;
    params->version_minor = 1;
    params->miu = 248U;
    params->wks = 0x0013U; /* Link Mgmt + SDP + SNEP */
    params->lto_ms = 1000U;
    params->rw = 4;
}

void llcp_link_init(llcp_link_t* link)
{
    if (!link) return;
    memset(link, 0, sizeof(*link));
    llcp_params_default(&link->local);
    llcp_params_default(&link->remote);
    llcp_params_default(&link->negotiated);

    for (int i = 0; i < 10; i++) {
        uint32_t r = esp_random();
        link->nfcid3[i] = (uint8_t)(r & 0xFFU);
    }

    link->did = 0;
    link->bs  = 0;
    link->br  = 0;
    link->pp  = llcp_pp_from_miu(link->local.miu);
    link->lr  = llcp_lr_from_pp(link->pp);
    link->pni = 0;
    link->llcp_active = false;
}

size_t llcp_tlv_version(uint8_t* out, size_t max, uint8_t version)
{
    if (!out || max < 3U) return 0;
    out[0] = LLCP_TLV_VERSION;
    out[1] = 1U;
    out[2] = version;
    return 3U;
}

size_t llcp_tlv_miux(uint8_t* out, size_t max, uint16_t miux)
{
    if (!out || max < 4U) return 0;
    uint16_t v = (uint16_t)(miux & 0x07FFU);
    out[0] = LLCP_TLV_MIUX;
    out[1] = 2U;
    out[2] = (uint8_t)((v >> 8) & 0xFFU);
    out[3] = (uint8_t)(v & 0xFFU);
    return 4U;
}

size_t llcp_tlv_wks(uint8_t* out, size_t max, uint16_t wks)
{
    if (!out || max < 4U) return 0;
    out[0] = LLCP_TLV_WKS;
    out[1] = 2U;
    out[2] = (uint8_t)((wks >> 8) & 0xFFU);
    out[3] = (uint8_t)(wks & 0xFFU);
    return 4U;
}

size_t llcp_tlv_lto(uint8_t* out, size_t max, uint8_t lto)
{
    if (!out || max < 3U) return 0;
    out[0] = LLCP_TLV_LTO;
    out[1] = 1U;
    out[2] = lto;
    return 3U;
}

size_t llcp_tlv_rw(uint8_t* out, size_t max, uint8_t rw)
{
    if (!out || max < 3U) return 0;
    out[0] = LLCP_TLV_RW;
    out[1] = 1U;
    out[2] = (uint8_t)(rw & 0x0FU);
    return 3U;
}

size_t llcp_tlv_sn(uint8_t* out, size_t max, const char* sn)
{
    if (!out || !sn) return 0;
    size_t len = strlen(sn);
    if (max < (2U + len)) return 0;
    out[0] = LLCP_TLV_SN;
    out[1] = (uint8_t)len;
    memcpy(&out[2], sn, len);
    return 2U + len;
}

size_t llcp_build_gt(const llcp_params_t* params, uint8_t* out, size_t max)
{
    if (!params || !out || max < 3U) return 0;

    size_t pos = 0;
    out[pos++] = LLCP_MAGIC_0;
    out[pos++] = LLCP_MAGIC_1;
    out[pos++] = LLCP_MAGIC_2;

    uint8_t ver = (uint8_t)((params->version_major << 4) |
                            (params->version_minor & 0x0FU));
    size_t n = llcp_tlv_version(&out[pos], max - pos, ver);
    if (n == 0) return 0;
    pos += n;

    n = llcp_tlv_wks(&out[pos], max - pos, params->wks);
    if (n == 0) return 0;
    pos += n;

    uint8_t lto = (uint8_t)((params->lto_ms + 9U) / 10U);
    n = llcp_tlv_lto(&out[pos], max - pos, lto);
    if (n == 0) return 0;
    pos += n;

    uint16_t miux = (params->miu > 128U) ? (uint16_t)(params->miu - 128U) : 0U;
    n = llcp_tlv_miux(&out[pos], max - pos, miux);
    if (n == 0) return 0;
    pos += n;

    n = llcp_tlv_rw(&out[pos], max - pos, params->rw);
    if (n == 0) return 0;
    pos += n;

    return pos;
}

bool llcp_parse_gt(const uint8_t* gt, size_t gt_len, llcp_params_t* out)
{
    if (!gt || gt_len < 3U || !out) return false;

    llcp_params_default(out);
    if (gt[0] != LLCP_MAGIC_0 || gt[1] != LLCP_MAGIC_1 || gt[2] != LLCP_MAGIC_2)
        return false;

    size_t pos = 3;
    while (pos + 2U <= gt_len) {
        uint8_t type = gt[pos++];
        uint8_t len = gt[pos++];
        if (pos + len > gt_len) break;

        switch (type) {
        case LLCP_TLV_VERSION:
            if (len >= 1) {
                out->version_major = (uint8_t)((gt[pos] >> 4) & 0x0FU);
                out->version_minor = (uint8_t)(gt[pos] & 0x0FU);
            }
            break;
        case LLCP_TLV_MIUX:
            if (len >= 2) {
                uint16_t miux = (uint16_t)((gt[pos] << 8) | gt[pos + 1]);
                miux &= 0x07FFU;
                out->miu = (uint16_t)(128U + miux);
            }
            break;
        case LLCP_TLV_WKS:
            if (len >= 2) {
                out->wks = (uint16_t)((gt[pos] << 8) | gt[pos + 1]);
            }
            break;
        case LLCP_TLV_LTO:
            if (len >= 1) {
                out->lto_ms = (uint16_t)(gt[pos] * 10U);
            }
            break;
        case LLCP_TLV_RW:
            if (len >= 1) {
                out->rw = (uint8_t)(gt[pos] & 0x0FU);
            }
            break;
        default:
            break;
        }

        pos += len;
    }

    return true;
}

static void llcp_compute_negotiated(llcp_link_t* link)
{
    if (!link) return;

    uint8_t maj = link->local.version_major < link->remote.version_major ?
                  link->local.version_major : link->remote.version_major;
    uint8_t min = (link->local.version_major == link->remote.version_major) ?
                  (link->local.version_minor < link->remote.version_minor ?
                   link->local.version_minor : link->remote.version_minor) : 0;

    link->negotiated.version_major = maj;
    link->negotiated.version_minor = min;
    link->negotiated.miu = (link->local.miu < link->remote.miu) ?
                           link->local.miu : link->remote.miu;
    link->negotiated.wks = (uint16_t)(link->local.wks & link->remote.wks);
    link->negotiated.lto_ms = (link->local.lto_ms < link->remote.lto_ms) ?
                              link->local.lto_ms : link->remote.lto_ms;
    link->negotiated.rw = (link->local.rw < link->remote.rw) ?
                          link->local.rw : link->remote.rw;
}

hb_nfc_err_t llcp_initiator_activate(llcp_link_t* link)
{
    if (!link) return HB_NFC_ERR_PARAM;

    nfc_rf_config_t cfg = {
        .tech           = NFC_RF_TECH_A,
        .tx_rate        = NFC_RF_BR_106,
        .rx_rate        = NFC_RF_BR_106,
        .am_mod_percent = 0,
        .tx_parity      = true,
        .rx_raw_parity  = false,
        .guard_time_us  = 0,
        .fdt_min_us     = 0,
        .validate_fdt   = false,
    };
    hb_nfc_err_t err = nfc_rf_apply(&cfg);
    if (err != HB_NFC_OK) return err;

    if (!st25r_field_is_on()) {
        err = st25r_field_on();
        if (err != HB_NFC_OK) return err;
    }

    uint8_t gt[64];
    size_t gt_len = llcp_build_gt(&link->local, gt, sizeof(gt));
    if (gt_len == 0) return HB_NFC_ERR_INTERNAL;

    uint8_t atr[96];
    size_t pos = 0;
    atr[pos++] = NFCDEP_CMD0_REQ;
    atr[pos++] = NFCDEP_CMD1_ATR_REQ;
    memcpy(&atr[pos], link->nfcid3, 10); pos += 10;
    atr[pos++] = link->did;
    atr[pos++] = link->bs;
    atr[pos++] = link->br;
    link->pp = llcp_pp_from_miu(link->local.miu);
    atr[pos++] = link->pp;
    memcpy(&atr[pos], gt, gt_len); pos += gt_len;

    uint8_t rx[128] = { 0 };
    int len = nfc_poller_transceive(atr, pos, true, rx, sizeof(rx), 1, 50);
    if (len < 17) return HB_NFC_ERR_NO_CARD;
    len = llcp_strip_crc(rx, len);

    if (rx[0] != NFCDEP_CMD0_RES || rx[1] != NFCDEP_CMD1_ATR_RES) {
        ESP_LOGW(TAG, "ATR_RES invalid: %02X %02X", rx[0], rx[1]);
        return HB_NFC_ERR_PROTOCOL;
    }

    link->did = rx[12];
    link->bs  = rx[13];
    link->br  = rx[14];
    link->to  = rx[15];
    link->pp  = rx[16];
    link->lr  = llcp_lr_from_pp(link->pp);
    link->rwt_ms = llcp_rwt_ms(link->to);

    size_t gt_off = 17U;
    if (gt_off < (size_t)len) {
        if (!llcp_parse_gt(&rx[gt_off], (size_t)len - gt_off, &link->remote)) {
            ESP_LOGW(TAG, "LLCP magic not found");
            return HB_NFC_ERR_PROTOCOL;
        }
    } else {
        return HB_NFC_ERR_PROTOCOL;
    }

    llcp_compute_negotiated(link);
    link->pni = 0;
    link->llcp_active = true;
    ESP_LOGI(TAG, "LLCP active: MIU=%u RW=%u LTO=%ums",
             (unsigned)link->negotiated.miu,
             (unsigned)link->negotiated.rw,
             (unsigned)link->negotiated.lto_ms);
    return HB_NFC_OK;
}

static int nfc_dep_transceive(llcp_link_t* link,
                               const uint8_t* tx, size_t tx_len,
                               uint8_t* rx, size_t rx_max,
                               int timeout_ms)
{
    if (!link || !tx || tx_len == 0 || !rx || rx_max == 0) return 0;

    uint16_t lr = link->lr ? link->lr : 64U;
    if (tx_len > lr) return 0;

    uint8_t frame[3 + 256];
    size_t pos = 0;
    frame[pos++] = NFCDEP_CMD0_REQ;
    frame[pos++] = NFCDEP_CMD1_DEP_REQ;
    frame[pos++] = (uint8_t)(link->pni & NFCDEP_PFB_PNI_MASK);
    memcpy(&frame[pos], tx, tx_len);
    pos += tx_len;

    uint8_t rbuf[512] = { 0 };
    int tmo = (timeout_ms > 0) ? timeout_ms : (int)(link->rwt_ms ? link->rwt_ms : 50U);
    int rlen = nfc_poller_transceive(frame, pos, true, rbuf, sizeof(rbuf), 1, tmo);
    if (rlen < 3) return 0;
    rlen = llcp_strip_crc(rbuf, rlen);

    if (rbuf[0] != NFCDEP_CMD0_RES || rbuf[1] != NFCDEP_CMD1_DEP_RES) {
        ESP_LOGW(TAG, "DEP_RES invalid: %02X %02X", rbuf[0], rbuf[1]);
        return 0;
    }

    uint8_t pfb = rbuf[2];
    size_t off = 3;
    if (pfb & NFCDEP_PFB_DID) off++;
    if (pfb & NFCDEP_PFB_NAD) off++;
    if (pfb & NFCDEP_PFB_MI) {
        ESP_LOGW(TAG, "DEP chaining not supported");
        return 0;
    }

    if (off >= (size_t)rlen) return 0;
    size_t payload_len = (size_t)rlen - off;
    if (payload_len > rx_max) payload_len = rx_max;
    memcpy(rx, &rbuf[off], payload_len);

    link->pni = (uint8_t)((link->pni + 1U) & 0x03U);
    return (int)payload_len;
}

int llcp_exchange_pdu(llcp_link_t* link,
                      const uint8_t* tx_pdu, size_t tx_len,
                      uint8_t* rx_pdu, size_t rx_max,
                      int timeout_ms)
{
    if (!link || !link->llcp_active) return 0;
    return nfc_dep_transceive(link, tx_pdu, tx_len, rx_pdu, rx_max, timeout_ms);
}

size_t llcp_build_header(uint8_t dsap, uint8_t ptype, uint8_t ssap, uint8_t* out, size_t max)
{
    if (!out || max < 2U) return 0;
    out[0] = (uint8_t)((dsap << 2) | ((ptype >> 2) & 0x03U));
    out[1] = (uint8_t)(((ptype & 0x03U) << 6) | (ssap & 0x3FU));
    return 2U;
}

bool llcp_parse_header(const uint8_t* pdu, size_t len,
                       uint8_t* dsap, uint8_t* ptype, uint8_t* ssap,
                       uint8_t* ns, uint8_t* nr)
{
    if (!pdu || len < 2U) return false;
    uint8_t d = (uint8_t)(pdu[0] >> 2);
    uint8_t p = (uint8_t)(((pdu[0] & 0x03U) << 2) | (pdu[1] >> 6));
    uint8_t s = (uint8_t)(pdu[1] & 0x3FU);

    if (dsap) *dsap = d;
    if (ptype) *ptype = p;
    if (ssap) *ssap = s;

    if ((p == LLCP_PTYPE_I || p == LLCP_PTYPE_RR || p == LLCP_PTYPE_RNR) && len >= 3U) {
        if (ns) *ns = (uint8_t)(pdu[2] >> 4);
        if (nr) *nr = (uint8_t)(pdu[2] & 0x0FU);
    }
    return true;
}

size_t llcp_build_symm(uint8_t* out, size_t max)
{
    return llcp_build_header(0, LLCP_PTYPE_SYMM, 0, out, max);
}

size_t llcp_build_ui(uint8_t dsap, uint8_t ssap,
                     const uint8_t* info, size_t info_len,
                     uint8_t* out, size_t max)
{
    size_t pos = llcp_build_header(dsap, LLCP_PTYPE_UI, ssap, out, max);
    if (pos == 0) return 0;
    if (!info || info_len == 0) return pos;
    if (pos + info_len > max) return 0;
    memcpy(&out[pos], info, info_len);
    return pos + info_len;
}

size_t llcp_build_connect(uint8_t dsap, uint8_t ssap, const char* service_name,
                          const llcp_params_t* params, uint8_t* out, size_t max)
{
    size_t pos = llcp_build_header(dsap, LLCP_PTYPE_CONNECT, ssap, out, max);
    if (pos == 0) return 0;

    llcp_params_t p;
    if (params) p = *params;
    else llcp_params_default(&p);

    size_t n = llcp_tlv_rw(&out[pos], max - pos, p.rw);
    if (n == 0) return 0;
    pos += n;

    uint16_t miux = (p.miu > 128U) ? (uint16_t)(p.miu - 128U) : 0U;
    n = llcp_tlv_miux(&out[pos], max - pos, miux);
    if (n == 0) return 0;
    pos += n;

    if (service_name) {
        n = llcp_tlv_sn(&out[pos], max - pos, service_name);
        if (n == 0) return 0;
        pos += n;
    }

    return pos;
}

size_t llcp_build_cc(uint8_t dsap, uint8_t ssap,
                     const llcp_params_t* params, uint8_t* out, size_t max)
{
    size_t pos = llcp_build_header(dsap, LLCP_PTYPE_CC, ssap, out, max);
    if (pos == 0) return 0;

    llcp_params_t p;
    if (params) p = *params;
    else llcp_params_default(&p);

    size_t n = llcp_tlv_rw(&out[pos], max - pos, p.rw);
    if (n == 0) return 0;
    pos += n;

    uint16_t miux = (p.miu > 128U) ? (uint16_t)(p.miu - 128U) : 0U;
    n = llcp_tlv_miux(&out[pos], max - pos, miux);
    if (n == 0) return 0;
    pos += n;

    return pos;
}

size_t llcp_build_disc(uint8_t dsap, uint8_t ssap, uint8_t* out, size_t max)
{
    return llcp_build_header(dsap, LLCP_PTYPE_DISC, ssap, out, max);
}

size_t llcp_build_dm(uint8_t dsap, uint8_t ssap, uint8_t reason, uint8_t* out, size_t max)
{
    size_t pos = llcp_build_header(dsap, LLCP_PTYPE_DM, ssap, out, max);
    if (pos == 0 || max < pos + 1U) return 0;
    out[pos++] = reason;
    return pos;
}

size_t llcp_build_i(uint8_t dsap, uint8_t ssap, uint8_t ns, uint8_t nr,
                    const uint8_t* info, size_t info_len,
                    uint8_t* out, size_t max)
{
    size_t pos = llcp_build_header(dsap, LLCP_PTYPE_I, ssap, out, max);
    if (pos == 0 || max < pos + 1U) return 0;
    out[pos++] = (uint8_t)(((ns & 0x0FU) << 4) | (nr & 0x0FU));
    if (info && info_len > 0) {
        if (pos + info_len > max) return 0;
        memcpy(&out[pos], info, info_len);
        pos += info_len;
    }
    return pos;
}

size_t llcp_build_rr(uint8_t dsap, uint8_t ssap, uint8_t nr, uint8_t* out, size_t max)
{
    size_t pos = llcp_build_header(dsap, LLCP_PTYPE_RR, ssap, out, max);
    if (pos == 0 || max < pos + 1U) return 0;
    out[pos++] = (uint8_t)(nr & 0x0FU);
    return pos;
}

size_t llcp_build_rnr(uint8_t dsap, uint8_t ssap, uint8_t nr, uint8_t* out, size_t max)
{
    size_t pos = llcp_build_header(dsap, LLCP_PTYPE_RNR, ssap, out, max);
    if (pos == 0 || max < pos + 1U) return 0;
    out[pos++] = (uint8_t)(nr & 0x0FU);
    return pos;
}
