/**
 * @file ndef.c
 * @brief NDEF parser/builder helpers.
 */
#include "ndef.h"

#include <string.h>
#include "esp_log.h"

#define TAG "ndef"

static const char* k_uri_prefixes[] = {
    "", "http://www.", "https://www.", "http://", "https://",
    "tel:", "mailto:", "ftp://anonymous:anonymous@", "ftp://ftp.",
    "ftps://", "sftp://", "smb://", "nfs://", "ftp://", "dav://",
    "news:", "telnet://", "imap:", "rtsp://", "urn:", "pop:",
    "sip:", "sips:", "tftp:", "btspp://", "btl2cap://", "btgoep://",
    "tcpobex://", "irdaobex://", "file://", "urn:epc:id:",
    "urn:epc:tag:", "urn:epc:pat:", "urn:epc:raw:", "urn:epc:",
    "urn:nfc:"
};

static const char* uri_prefix(uint8_t code)
{
    if (code < (sizeof(k_uri_prefixes) / sizeof(k_uri_prefixes[0]))) {
        return k_uri_prefixes[code];
    }
    return "";
}

static uint8_t uri_prefix_match(const char* uri, size_t uri_len, size_t* prefix_len)
{
    uint8_t best = 0;
    size_t best_len = 0;
    if (!uri) {
        if (prefix_len) *prefix_len = 0;
        return 0;
    }

    uint8_t count = (uint8_t)(sizeof(k_uri_prefixes) / sizeof(k_uri_prefixes[0]));
    for (uint8_t i = 1; i < count; i++) {
        const char* p = k_uri_prefixes[i];
        if (!p || !p[0]) continue;
        size_t l = strlen(p);
        if (l == 0 || l > uri_len) continue;
        if (strncmp(uri, p, l) == 0 && l > best_len) {
            best_len = l;
            best = i;
        }
        if (l == uri_len && best_len == uri_len) break;
    }

    if (prefix_len) *prefix_len = best_len;
    return best;
}

static void log_str(const char* label, const uint8_t* data, size_t len)
{
    char buf[96];
    size_t n = (len < (sizeof(buf) - 1)) ? len : (sizeof(buf) - 1);
    memcpy(buf, data, n);
    buf[n] = '\0';
    ESP_LOGI(TAG, "%s %s", label, buf);
}

void ndef_parser_init(ndef_parser_t* parser, const uint8_t* data, size_t len)
{
    if (!parser) return;
    parser->data = data;
    parser->len = len;
    parser->pos = 0;
    parser->done = false;
    parser->err = HB_NFC_OK;
}

bool ndef_parse_next(ndef_parser_t* parser, ndef_record_t* rec)
{
    if (!parser || !rec || parser->done) return false;
    if (!parser->data || parser->len < 3U || parser->pos >= parser->len) {
        parser->done = true;
        parser->err = HB_NFC_OK;
        return false;
    }

    const uint8_t* data = parser->data;
    size_t len = parser->len;
    size_t pos = parser->pos;

    if (pos >= len) {
        parser->done = true;
        parser->err = HB_NFC_OK;
        return false;
    }

    uint8_t hdr = data[pos++];
    rec->mb = (hdr & 0x80U) != 0;
    rec->me = (hdr & 0x40U) != 0;
    rec->cf = (hdr & 0x20U) != 0;
    rec->sr = (hdr & 0x10U) != 0;
    rec->il = (hdr & 0x08U) != 0;
    rec->tnf = (uint8_t)(hdr & 0x07U);

    if (rec->cf) {
        parser->err = HB_NFC_ERR_PROTOCOL;
        parser->done = true;
        return false;
    }

    if (pos >= len) {
        parser->err = HB_NFC_ERR_PROTOCOL;
        parser->done = true;
        return false;
    }
    rec->type_len = data[pos++];

    if (rec->sr) {
        if (pos >= len) {
            parser->err = HB_NFC_ERR_PROTOCOL;
            parser->done = true;
            return false;
        }
        rec->payload_len = data[pos++];
    } else {
        if (pos + 4U > len) {
            parser->err = HB_NFC_ERR_PROTOCOL;
            parser->done = true;
            return false;
        }
        rec->payload_len = ((uint32_t)data[pos] << 24) |
                           ((uint32_t)data[pos + 1] << 16) |
                           ((uint32_t)data[pos + 2] << 8) |
                           (uint32_t)data[pos + 3];
        pos += 4U;
    }

    rec->id_len = 0;
    if (rec->il) {
        if (pos >= len) {
            parser->err = HB_NFC_ERR_PROTOCOL;
            parser->done = true;
            return false;
        }
        rec->id_len = data[pos++];
    }

    size_t needed = (size_t)rec->type_len + (size_t)rec->id_len + (size_t)rec->payload_len;
    if (pos + needed > len) {
        parser->err = HB_NFC_ERR_PROTOCOL;
        parser->done = true;
        return false;
    }

    rec->type = &data[pos];
    pos += rec->type_len;
    rec->id = &data[pos];
    pos += rec->id_len;
    rec->payload = &data[pos];
    pos += rec->payload_len;

    parser->pos = pos;
    if (rec->me || parser->pos >= parser->len) parser->done = true;
    parser->err = HB_NFC_OK;
    return true;
}

hb_nfc_err_t ndef_parser_error(const ndef_parser_t* parser)
{
    if (!parser) return HB_NFC_ERR_PARAM;
    return parser->err;
}

bool ndef_record_is_text(const ndef_record_t* rec)
{
    return rec && rec->tnf == NDEF_TNF_WELL_KNOWN &&
           rec->type_len == 1 && rec->type && rec->type[0] == 'T';
}

bool ndef_record_is_uri(const ndef_record_t* rec)
{
    return rec && rec->tnf == NDEF_TNF_WELL_KNOWN &&
           rec->type_len == 1 && rec->type && rec->type[0] == 'U';
}

bool ndef_record_is_smart_poster(const ndef_record_t* rec)
{
    return rec && rec->tnf == NDEF_TNF_WELL_KNOWN &&
           rec->type_len == 2 && rec->type &&
           rec->type[0] == 'S' && rec->type[1] == 'p';
}

bool ndef_record_is_mime(const ndef_record_t* rec)
{
    return rec && rec->tnf == NDEF_TNF_MIME && rec->type_len > 0 && rec->type;
}

bool ndef_decode_text(const ndef_record_t* rec, ndef_text_t* out)
{
    if (!rec || !out || !ndef_record_is_text(rec)) return false;
    if (rec->payload_len < 1U) return false;
    uint8_t status = rec->payload[0];
    uint8_t lang_len = (uint8_t)(status & 0x3FU);
    if (1U + lang_len > rec->payload_len) return false;
    out->utf16 = (status & 0x80U) != 0;
    out->lang = (lang_len > 0) ? (const uint8_t*)(rec->payload + 1) : NULL;
    out->lang_len = lang_len;
    out->text = rec->payload + 1U + lang_len;
    out->text_len = (size_t)rec->payload_len - 1U - lang_len;
    return true;
}

bool ndef_decode_uri(const ndef_record_t* rec, char* out, size_t out_max, size_t* out_len)
{
    if (!rec || !ndef_record_is_uri(rec) || rec->payload_len < 1U) return false;
    uint8_t prefix_code = rec->payload[0];
    const char* prefix = uri_prefix(prefix_code);
    size_t prefix_len = strlen(prefix);
    size_t rest_len = (size_t)rec->payload_len - 1U;
    size_t total = prefix_len + rest_len;
    if (out_len) *out_len = total;
    if (!out || out_max == 0) return true;
    if (out_max < total + 1U) return false;
    if (prefix_len > 0) memcpy(out, prefix, prefix_len);
    if (rest_len > 0) memcpy(out + prefix_len, rec->payload + 1, rest_len);
    out[total] = '\0';
    return true;
}

void ndef_builder_init(ndef_builder_t* b, uint8_t* out, size_t max)
{
    if (!b) return;
    b->buf = out;
    b->max = max;
    b->len = 0;
    b->last_hdr_off = 0;
    b->has_record = false;
}

size_t ndef_builder_len(const ndef_builder_t* b)
{
    return b ? b->len : 0;
}

static hb_nfc_err_t ndef_builder_begin_record(ndef_builder_t* b,
                                              uint8_t tnf,
                                              const uint8_t* type, uint8_t type_len,
                                              const uint8_t* id, uint8_t id_len,
                                              uint32_t payload_len,
                                              uint8_t** payload_out)
{
    if (!b || !b->buf) return HB_NFC_ERR_PARAM;
    if (payload_len > 0 && !payload_out) return HB_NFC_ERR_PARAM;
    if (type_len > 0 && !type) return HB_NFC_ERR_PARAM;
    if (id_len > 0 && !id) return HB_NFC_ERR_PARAM;

    bool sr = payload_len <= 0xFFU;
    bool il = id && (id_len > 0);
    size_t needed = 1U + 1U + (sr ? 1U : 4U) + (il ? 1U : 0U) +
                    type_len + id_len + (size_t)payload_len;
    if (b->len + needed > b->max) return HB_NFC_ERR_PARAM;

    if (b->has_record) {
        b->buf[b->last_hdr_off] &= (uint8_t)~0x40U; /* clear ME */
    }

    size_t hdr_off = b->len;
    uint8_t hdr = (uint8_t)(tnf & 0x07U);
    if (!b->has_record) hdr |= 0x80U; /* MB */
    hdr |= 0x40U; /* ME for now */
    if (sr) hdr |= 0x10U;
    if (il) hdr |= 0x08U;

    b->buf[b->len++] = hdr;
    b->buf[b->len++] = type_len;
    if (sr) {
        b->buf[b->len++] = (uint8_t)payload_len;
    } else {
        b->buf[b->len++] = (uint8_t)((payload_len >> 24) & 0xFFU);
        b->buf[b->len++] = (uint8_t)((payload_len >> 16) & 0xFFU);
        b->buf[b->len++] = (uint8_t)((payload_len >> 8) & 0xFFU);
        b->buf[b->len++] = (uint8_t)(payload_len & 0xFFU);
    }
    if (il) b->buf[b->len++] = id_len;

    if (type_len > 0) {
        memcpy(&b->buf[b->len], type, type_len);
        b->len += type_len;
    }
    if (id_len > 0) {
        memcpy(&b->buf[b->len], id, id_len);
        b->len += id_len;
    }

    if (payload_out) *payload_out = &b->buf[b->len];
    b->len += payload_len;
    b->last_hdr_off = hdr_off;
    b->has_record = true;
    return HB_NFC_OK;
}

hb_nfc_err_t ndef_builder_add_record(ndef_builder_t* b,
                                     uint8_t tnf,
                                     const uint8_t* type, uint8_t type_len,
                                     const uint8_t* id, uint8_t id_len,
                                     const uint8_t* payload, uint32_t payload_len)
{
    if (payload_len > 0 && !payload) return HB_NFC_ERR_PARAM;
    uint8_t* dst = NULL;
    hb_nfc_err_t err = ndef_builder_begin_record(b, tnf, type, type_len,
                                                 id, id_len, payload_len, &dst);
    if (err != HB_NFC_OK) return err;
    if (payload_len > 0 && payload) memcpy(dst, payload, payload_len);
    return HB_NFC_OK;
}

hb_nfc_err_t ndef_builder_add_text(ndef_builder_t* b,
                                   const char* text,
                                   const char* lang,
                                   bool utf16)
{
    if (!b || !text) return HB_NFC_ERR_PARAM;
    size_t text_len = strlen(text);
    size_t lang_len = lang ? strlen(lang) : 0;
    if (lang_len > 63U) return HB_NFC_ERR_PARAM;

    uint32_t payload_len = (uint32_t)(1U + lang_len + text_len);
    uint8_t* payload = NULL;
    hb_nfc_err_t err = ndef_builder_begin_record(b, NDEF_TNF_WELL_KNOWN,
                                                 (const uint8_t*)"T", 1,
                                                 NULL, 0, payload_len, &payload);
    if (err != HB_NFC_OK) return err;

    payload[0] = (uint8_t)((utf16 ? 0x80U : 0x00U) | (uint8_t)lang_len);
    if (lang_len > 0) memcpy(&payload[1], lang, lang_len);
    if (text_len > 0) memcpy(&payload[1 + lang_len], text, text_len);
    return HB_NFC_OK;
}

hb_nfc_err_t ndef_builder_add_uri(ndef_builder_t* b, const char* uri)
{
    if (!b || !uri) return HB_NFC_ERR_PARAM;
    size_t uri_len = strlen(uri);
    size_t prefix_len = 0;
    uint8_t prefix_code = uri_prefix_match(uri, uri_len, &prefix_len);

    uint32_t payload_len = (uint32_t)(1U + (uri_len - prefix_len));
    uint8_t* payload = NULL;
    hb_nfc_err_t err = ndef_builder_begin_record(b, NDEF_TNF_WELL_KNOWN,
                                                 (const uint8_t*)"U", 1,
                                                 NULL, 0, payload_len, &payload);
    if (err != HB_NFC_OK) return err;

    payload[0] = prefix_code;
    if (uri_len > prefix_len) {
        memcpy(&payload[1], uri + prefix_len, uri_len - prefix_len);
    }
    return HB_NFC_OK;
}

hb_nfc_err_t ndef_builder_add_mime(ndef_builder_t* b,
                                   const char* mime_type,
                                   const uint8_t* data, size_t data_len)
{
    if (!b || !mime_type) return HB_NFC_ERR_PARAM;
    size_t type_len = strlen(mime_type);
    if (type_len == 0 || type_len > 0xFFU) return HB_NFC_ERR_PARAM;
    return ndef_builder_add_record(b, NDEF_TNF_MIME,
                                   (const uint8_t*)mime_type, (uint8_t)type_len,
                                   NULL, 0,
                                   data, (uint32_t)data_len);
}

hb_nfc_err_t ndef_builder_add_smart_poster(ndef_builder_t* b,
                                           const uint8_t* nested_ndef,
                                           size_t nested_len)
{
    if (!b || !nested_ndef || nested_len == 0) return HB_NFC_ERR_PARAM;
    return ndef_builder_add_record(b, NDEF_TNF_WELL_KNOWN,
                                   (const uint8_t*)"Sp", 2,
                                   NULL, 0,
                                   nested_ndef, (uint32_t)nested_len);
}

ndef_tlv_status_t ndef_tlv_find(const uint8_t* data, size_t len,
                                uint8_t type, ndef_tlv_t* out)
{
    if (!data || len == 0 || !out) return NDEF_TLV_STATUS_INVALID;
    size_t pos = 0;
    while (pos < len) {
        size_t start = pos;
        uint8_t t = data[pos++];
        if (t == NDEF_TLV_NULL) continue;
        if (t == NDEF_TLV_TERMINATOR) return NDEF_TLV_STATUS_NOT_FOUND;
        if (pos >= len) return NDEF_TLV_STATUS_INVALID;

        size_t l = 0;
        if (data[pos] == 0xFFU) {
            if (pos + 2U >= len) return NDEF_TLV_STATUS_INVALID;
            l = ((size_t)data[pos + 1] << 8) | data[pos + 2];
            pos += 3U;
        } else {
            l = data[pos++];
        }

        if (pos + l > len) return NDEF_TLV_STATUS_INVALID;
        if (t == type) {
            out->type = t;
            out->length = l;
            out->value = &data[pos];
            out->tlv_len = (pos - start) + l;
            return NDEF_TLV_STATUS_OK;
        }
        pos += l;
    }
    return NDEF_TLV_STATUS_NOT_FOUND;
}

ndef_tlv_status_t ndef_tlv_find_ndef(const uint8_t* data, size_t len,
                                     ndef_tlv_t* out)
{
    return ndef_tlv_find(data, len, NDEF_TLV_NDEF, out);
}

ndef_tlv_status_t ndef_tlv_build(uint8_t* out, size_t max,
                                 uint8_t type,
                                 const uint8_t* value, size_t value_len,
                                 size_t* out_len)
{
    if (!out || max == 0) return NDEF_TLV_STATUS_INVALID;
    if (value_len > 0xFFFFU) return NDEF_TLV_STATUS_INVALID;

    size_t len_field = (value_len < 0xFFU) ? 1U : 3U;
    size_t needed = 1U + len_field + value_len;
    if (needed > max) return NDEF_TLV_STATUS_INVALID;

    size_t pos = 0;
    out[pos++] = type;
    if (value_len < 0xFFU) {
        out[pos++] = (uint8_t)value_len;
    } else {
        out[pos++] = 0xFFU;
        out[pos++] = (uint8_t)((value_len >> 8) & 0xFFU);
        out[pos++] = (uint8_t)(value_len & 0xFFU);
    }
    if (value_len > 0 && value) {
        memcpy(&out[pos], value, value_len);
    }
    pos += value_len;
    if (out_len) *out_len = pos;
    return NDEF_TLV_STATUS_OK;
}

ndef_tlv_status_t ndef_tlv_build_ndef(uint8_t* out, size_t max,
                                      const uint8_t* ndef, size_t ndef_len,
                                      size_t* out_len)
{
    if (!out || max == 0) return NDEF_TLV_STATUS_INVALID;
    size_t tlv_len = 0;
    ndef_tlv_status_t st = ndef_tlv_build(out, max, NDEF_TLV_NDEF, ndef, ndef_len, &tlv_len);
    if (st != NDEF_TLV_STATUS_OK) return st;
    if (tlv_len + 1U > max) return NDEF_TLV_STATUS_INVALID;
    out[tlv_len++] = NDEF_TLV_TERMINATOR;
    if (out_len) *out_len = tlv_len;
    return NDEF_TLV_STATUS_OK;
}

void ndef_print(const uint8_t* data, size_t len)
{
    if (!data || len < 3) {
        ESP_LOGW(TAG, "NDEF empty/invalid");
        return;
    }

    ndef_parser_t parser;
    ndef_parser_init(&parser, data, len);
    ndef_record_t rec;
    int idx = 0;

    while (ndef_parse_next(&parser, &rec)) {
        ESP_LOGI(TAG, "Record %d: MB=%d ME=%d SR=%d TNF=0x%02X",
                 idx, rec.mb, rec.me, rec.sr, rec.tnf);

        if (ndef_record_is_text(&rec)) {
            ndef_text_t text;
            if (ndef_decode_text(&rec, &text)) {
                ESP_LOGI(TAG, "NDEF Text (%s):", text.utf16 ? "UTF-16" : "UTF-8");
                if (text.lang_len > 0 && text.lang) {
                    log_str("Lang:", text.lang, text.lang_len);
                }
                if (text.text_len > 0 && text.text) {
                    log_str("Text:", text.text, text.text_len);
                }
            }
        } else if (ndef_record_is_uri(&rec)) {
            char buf[128];
            size_t out_len = 0;
            if (ndef_decode_uri(&rec, buf, sizeof(buf), &out_len)) {
                ESP_LOGI(TAG, "NDEF URI (%u bytes): %s", (unsigned)out_len, buf);
            } else {
                ESP_LOGI(TAG, "NDEF URI (len=%u)", (unsigned)rec.payload_len);
            }
        } else if (ndef_record_is_mime(&rec)) {
            log_str("MIME:", rec.type, rec.type_len);
            ESP_LOGI(TAG, "Payload len=%u", (unsigned)rec.payload_len);
        } else if (ndef_record_is_smart_poster(&rec)) {
            ESP_LOGI(TAG, "NDEF Smart Poster (payload=%u bytes)", (unsigned)rec.payload_len);
        } else {
            ESP_LOGI(TAG, "NDEF record type len=%u payload=%u",
                     rec.type_len, (unsigned)rec.payload_len);
        }

        idx++;
        if (rec.me) break;
    }

    if (ndef_parser_error(&parser) != HB_NFC_OK) {
        ESP_LOGW(TAG, "NDEF parse error");
    }
}
