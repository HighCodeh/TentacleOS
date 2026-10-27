/* Actual adapter + actual cJSON; explicit SDK CRYPTO/transport/store fakes.
 * This is not an AES/P256 test or proof of phone compatibility. All strings are
 * synthetic fixtures, never usable credentials. One process per test case.
 */
#include "hb_muse_pairing.h"
#include "link_pairing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)
static unsigned sdk_state, generation, crypto_generation, rx_counter;
static unsigned sent, encrypted, stored, confirmations, arms, hello_calls;
static bool connected = true, notify_ok = true, persist_ok = true, expire_sdk;
static bool revoke_send, revoke_store, reject_crypto, wrong_auth;
static const char *next_plain;
static size_t next_plain_bytes;
static char last_status[64];
static bool maker_seen;
static bool maximum_tokens;
static unsigned decrypt_calls;
static uint64_t live_now;
static bool expire_send, expire_store;
static unsigned commits;
static unsigned scan_calls, scan_results;
static unsigned scan_fault;
static unsigned wifi_calls, wifi_fault;
static unsigned completion_stage;
static uint64_t now_ms(void *user) { (void)user; return live_now; }
/* Deterministic fake scheduler updates its clock at each public API entry. */
static uint64_t at(uint64_t now) { live_now = now; return now; }
#define hb_muse_pairing_open(w,n) hb_muse_pairing_open((w),at(n))
#define hb_muse_pairing_connected(c,n) hb_muse_pairing_connected((c),at(n))
#define hb_muse_pairing_receive(c,j,b,n) hb_muse_pairing_receive((c),(j),(b),at(n))
#define hb_muse_pairing_button(t,e,p,n) hb_muse_pairing_button((t),(e),(p),at(n))
#define hb_muse_pairing_poll(n) hb_muse_pairing_poll(at(n))
static const char hello[] = "{\"action\":\"pairing_client_hello\",\"version\":5,\"pairing_auth\":\"none\",\"pairing_policy\":\"confirm_press\",\"mobile_pub\":\"fixture\",\"mobile_nonce\":\"fixture\"}";
static const char finished[] = "{\"action\":\"pairing_client_finished\"}";
static const char credentials[] = "{\"action\":\"provision_v2\",\"ssid\":\"fixture-network\",\"password\":\"fixture-password\",\"access_token\":\"fixture-access\",\"refresh_token\":\"fixture-refresh\",\"token_type\":\"device\"}";
static char *duplicate(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); CHECK(p); memcpy(p, s, n + 1); return p; }

/* Real SDK declarations are included; only their implementations are faked. */
void link_pairing_init(const char *node, const char *device, const char *mac, const char *version, const char *token) {
    CHECK(node && device && mac && version && !token); link_pairing_reset();
}
void link_pairing_reset(void) { ++generation; crypto_generation = generation; sdk_state = 0; rx_counter = 0; }
void link_pairing_add_device_info(cJSON *root) {
    cJSON_AddNumberToObject(root, "pairing_protocol", 5);
    cJSON_AddNumberToObject(root, "pairing_auth_epoch", 0);
    cJSON_AddStringToObject(root, "pairing_auth", wrong_auth ? "fleet_ecdsa_p256_v1" : "none");
    cJSON_AddStringToObject(root, "pairing_policy", "confirm_press");
}
const char *link_pairing_handle_client_hello(cJSON *root, char **reply) {
    CHECK(root && reply); ++hello_calls;
    if (reject_crypto) { *reply = NULL; return "error_pairing_invalid_hello"; }
    link_pairing_reset(); sdk_state = 1;
    *reply = duplicate("{\"type\":\"pairing_ready\",\"version\":5}"); return NULL;
}
const char *link_pairing_decrypt_command(cJSON *root, char **plain) {
    ++decrypt_calls;
    const cJSON *counter = cJSON_GetObjectItemCaseSensitive(root, "counter");
    *plain = NULL;
    if (reject_crypto || expire_sdk || !sdk_state || !cJSON_IsString(counter) ||
        strtoul(counter->valuestring, NULL, 10) != rx_counter || !next_plain) {
        link_pairing_reset(); return "error_pairing_decrypt";
    }
    *plain = calloc(1, next_plain_bytes + 1); CHECK(*plain);
    memcpy(*plain, next_plain, next_plain_bytes); ++rx_counter; return NULL;
}
uint32_t link_pairing_handle_client_finished(void) {
    if (expire_sdk || sdk_state != 1 || rx_counter != 1) return 0;
    sdk_state = 2; return ++generation;
}
bool link_pairing_confirmation_required(void) { return !expire_sdk && sdk_state == 2; }
bool link_pairing_arm_confirmation(uint32_t wanted) {
    CHECK(!strcmp(last_status, "confirm_required") && sent >= 2);
    if (wanted != generation || !link_pairing_confirmation_required()) return false;
    ++arms; return true;
}
uint32_t link_pairing_confirm_active_session(void) {
    if (!arms || !link_pairing_confirmation_required()) return 0;
    ++confirmations; sdk_state = 3; return ++generation;
}
bool link_pairing_session_confirmed(void) { return !expire_sdk && (sdk_state == 3 || sdk_state == 4); }
uint32_t link_pairing_mark_provisioning_active(void) {
    if (!link_pairing_session_confirmed() || sdk_state != 3) return 0;
    sdk_state = 4; return ++generation;
}
bool link_pairing_provisioning_session_valid(uint32_t wanted) { return !expire_sdk && sdk_state == 4 && wanted == generation; }
uint32_t link_pairing_session_generation(void) { return sdk_state ? generation : 0; }
bool link_pairing_session_is_current(uint32_t wanted) { return sdk_state && wanted && wanted == generation; }
bool link_pairing_record_session_is_current(uint32_t wanted) { return sdk_state && wanted && wanted == crypto_generation; }
char *link_pairing_encrypt_json(const char *plain, uint32_t wanted, uint32_t *record) {
    if (!link_pairing_session_is_current(wanted) || expire_sdk) return NULL;
    cJSON *root = cJSON_Parse(plain); CHECK(root);
    const cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    const cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "sdk_token");
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type) && !strcmp(type->valuestring, "wifi_scan_result")) {
        const cJSON *networks = cJSON_GetObjectItemCaseSensitive(root, "networks");
        CHECK(cJSON_IsArray(networks) && cJSON_GetArraySize(networks) == 1 && !status && !token);
        const cJSON *entry = cJSON_GetArrayItem(networks, 0);
        CHECK(!strcmp(cJSON_GetObjectItemCaseSensitive(entry, "ssid")->valuestring, "fixture-network"));
        CHECK(cJSON_GetObjectItemCaseSensitive(entry, "rssi")->valueint == -55);
        CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(entry, "secure")));
        ++scan_results;
    } else {
        CHECK(cJSON_IsString(status)); CHECK(strlen(status->valuestring) < sizeof(last_status));
        strcpy(last_status, status->valuestring);
        if (!strcmp(last_status, "wifi_connecting")) { CHECK(completion_stage == 0); completion_stage = 1; }
        if (!strcmp(last_status, "wifi_connected")) { CHECK(completion_stage == 2 && wifi_calls == 1); completion_stage = 3; }
        if (!strcmp(last_status, "auth_ok")) { CHECK(completion_stage == 3 && commits == 1); completion_stage = 4; }
    }
    if (token) {
        CHECK(!strcmp(last_status, "pairing_confirmed") && confirmations == 1);
        CHECK(!strcmp(token->valuestring, "mgst_fixture_not_valid")); maker_seen = true;
    }
    cJSON_Delete(root); *record = crypto_generation; ++encrypted;
    return duplicate("{\"type\":\"pairing_encrypted\",\"ciphertext\":\"EXPLICIT_FAKE\"}");
}
static bool current(void *u, uint64_t window, uint64_t connection) {
    (void)u; return connected && window && connection;
}
static bool send_message(void *u, uint64_t connection, const char *json, size_t bytes, uint32_t record) {
    (void)u; CHECK(connection && json && bytes == strlen(json));
    if (record) CHECK(link_pairing_record_session_is_current(record));
    ++sent;
    if (revoke_send) connected = false;
    if (expire_send) live_now = HB_MUSE_PAIRING_WINDOW_MS;
    return notify_ok;
}
static const char *expected_refresh;
static bool store(void *u, const hb_muse_pairing_credentials_t *c, bool (*guard)(void *), void *g) {
    (void)u; CHECK(guard(g) && completion_stage == 3); ++stored;
    CHECK(!strcmp(c->ssid, "fixture-network") && !c->noise_host[0]);
    if (maximum_tokens) {
        CHECK(strlen(c->access_token) == HB_MUSE_PAIRING_TOKEN_CAP - 1u);
        CHECK(strlen(c->refresh_token) == HB_MUSE_PAIRING_TOKEN_CAP - 1u);
    } else if (expected_refresh) {
        CHECK(!strcmp(c->access_token, "fixture-access") && !strcmp(c->refresh_token, expected_refresh));
    } else {
        CHECK(!strcmp(c->access_token, "fixture-access") && !strcmp(c->refresh_token, "fixture-refresh"));
    }
    if (revoke_store) connected = false;
    if (expire_store) live_now = HB_MUSE_PAIRING_WINDOW_MS;
    bool ok = persist_ok && guard(g);
    if (ok) ++commits;
    return ok;
}
static bool scan(void *u, hb_muse_pairing_network_t *out, size_t capacity, size_t *count,
                 bool (*guard)(void *), void *g) {
    (void)u; CHECK(guard(g) && capacity == HB_MUSE_PAIRING_NETWORK_MAX); ++scan_calls;
    strcpy(out[0].ssid, "fixture-network"); out[0].rssi = -55; out[0].secure = true; *count = 1;
    if (scan_fault == 1) return false;
    if (scan_fault == 2) *count = capacity + 1u;
    if (scan_fault == 3) memset(out[0].ssid, 'A', sizeof(out[0].ssid));
    if (scan_fault == 4) out[0].rssi = 1;
    if (scan_fault == 5) live_now += HB_MUSE_PAIRING_SCAN_TIMEOUT_MS;
    if (scan_fault == 8) live_now += 8000u;
    if (scan_fault == 6) connected = false;
    if (scan_fault == 7) live_now--;
    return true;
}
static bool connect_wifi(void *u, const hb_muse_pairing_credentials_t *c, bool (*guard)(void *), void *g) {
    (void)u; CHECK(guard(g) && completion_stage == 1 && !strcmp(c->ssid, "fixture-network")); ++wifi_calls;
    if (wifi_fault == 1) return false;
    if (wifi_fault == 2) live_now += 15000u;
    if (wifi_fault == 3) connected = false;
    completion_stage = 2; return true;
}
static void initialize(bool provider) {
    hb_muse_pairing_config_t cfg = {now_ms, current, send_message, provider ? store : NULL, NULL,
        "node-fixture", "device-fixture", "00:11:22:33:44:55", "fixture", "mgst_fixture_not_valid", scan, connect_wifi};
    CHECK(hb_muse_pairing_init(&cfg));
    CHECK(!hb_muse_pairing_init(&cfg));
    CHECK(hb_muse_pairing_open(1, 0)); CHECK(hb_muse_pairing_connected(1, 1));
}
static bool receive(const char *json, uint64_t now) {
    return hb_muse_pairing_receive(1, (const uint8_t *)json, strlen(json), now);
}
static bool encrypted_command(const char *plain, size_t n, uint64_t now, unsigned counter) {
    /* Correct unpadded base64 length, deliberately fake content. Real SDK
     * decrypt is replaced above; no claim that this ciphertext is valid. */
    size_t encoded = (n / 3u) * 4u + (n % 3u ? n % 3u + 1u : 0u);
    char *cipher = malloc(encoded + 1u); CHECK(cipher); memset(cipher, 'A', encoded); cipher[encoded] = 0;
    cJSON *root = cJSON_CreateObject(); char number[32]; snprintf(number, sizeof(number), "%u", counter);
    cJSON_AddStringToObject(root, "action", "pairing_encrypted"); cJSON_AddStringToObject(root, "counter", number);
    cJSON_AddStringToObject(root, "ciphertext", cipher); cJSON_AddStringToObject(root, "tag", "EXPLICIT_FAKE");
    char *json = cJSON_PrintUnformatted(root); CHECK(json); cJSON_Delete(root); free(cipher);
    next_plain = plain; next_plain_bytes = n;
    bool ok = receive(json, now); free(json); next_plain = NULL; return ok;
}
static void prompt(void) {
    CHECK(receive(hello, 2)); CHECK(!confirmations && !maker_seen);
    CHECK(encrypted_command(finished, strlen(finished), 3, 0));
    CHECK(hb_muse_pairing_status().phase == HB_MUSE_PAIRING_CONFIRM && arms == 1 && !maker_seen);
}
static void confirm(void) {
    prompt(); uint64_t ticket = hb_muse_pairing_status().confirmation_ticket;
    CHECK(!hb_muse_pairing_button(ticket, 1, true, 4));
    CHECK(hb_muse_pairing_button(ticket, 2, false, 5));
    CHECK(!hb_muse_pairing_button(ticket, 2, true, 5));
    CHECK(hb_muse_pairing_button(ticket, 3, true, 6));
    CHECK(maker_seen && confirmations == 1 && hb_muse_pairing_status().phase == HB_MUSE_PAIRING_CONFIRMED);
}
static const char *const cases[] = {"success", "no-provider", "early-provision", "held-button", "stale-ticket",
    "failed-prompt", "disconnect-send", "disconnect-store", "store-fails", "decrypt-fails", "replay",
    "expiry", "rollback", "duplicate-json", "trailing-json", "nul-json", "finished-extra",
    "api-override", "legacy-api-ignored", "legacy-api-control", "legacy-api-type", "legacy-api-oversize", "noise-override", "ota-blocked", "plaintext-provision", "nested-json",
    "token-whitespace", "window-generation", "sdk-expiry", "embedded-plain-nul", "wrong-community",
    "padded-cipher", "cipher-whitespace", "cipher-too-large", "maximum-tokens", "oversize-token",
    "refresh-opaque", "refresh-long", "refresh-limit", "refresh-overflow", "refresh-control", "refresh-space", "refresh-empty-suffix",
    "expire-during-store", "expire-during-send", "scan-success", "scan-failure", "scan-count",
    "scan-ssid", "scan-rssi", "scan-timeout", "scan-revoked", "scan-clock", "scan-early",
    "scan-plaintext", "scan-extra", "scan-slow-success", "wifi-failure", "wifi-timeout", "wifi-revoke"};
int main(int argc, char **argv) {
    if (argc == 1) { for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) puts(cases[i]); return 0; }
    CHECK(argc == 2); const char *name = argv[1];
    if (!strcmp(name, "wrong-community")) {
        wrong_auth = true;
        hb_muse_pairing_config_t cfg = {now_ms, current, send_message, store, NULL,
            "node", "device", "00:11:22:33:44:55", "fixture", "mgst_fixture_not_valid", scan, connect_wifi};
        CHECK(!hb_muse_pairing_init(&cfg)); puts("PASS wrong-community"); return 0;
    }
    initialize(strcmp(name, "no-provider") != 0);
    if (!strncmp(name, "wifi-", 5)) {
        confirm(); wifi_fault = !strcmp(name, "wifi-failure") ? 1u : !strcmp(name, "wifi-timeout") ? 2u : 3u;
        CHECK(!encrypted_command(credentials, strlen(credentials), 7, 1));
        CHECK(wifi_calls == 1 && !stored && completion_stage < 3);
    } else if (!strncmp(name, "scan-", 5)) {
        const char *request = "{\"action\":\"wifi_scan\"}";
        if (!strcmp(name, "scan-early")) prompt(); else confirm();
        if (!strcmp(name, "scan-plaintext")) { CHECK(!receive(request, 7)); CHECK(!scan_calls); }
        else {
            if (!strcmp(name, "scan-extra")) request = "{\"action\":\"wifi_scan\",\"extra\":1}";
            if (!strcmp(name, "scan-failure")) scan_fault = 1;
            if (!strcmp(name, "scan-count")) scan_fault = 2;
            if (!strcmp(name, "scan-ssid")) scan_fault = 3;
            if (!strcmp(name, "scan-rssi")) scan_fault = 4;
            if (!strcmp(name, "scan-timeout")) scan_fault = 5;
            if (!strcmp(name, "scan-revoked")) scan_fault = 6;
            if (!strcmp(name, "scan-clock")) scan_fault = 7;
            if (!strcmp(name, "scan-slow-success")) scan_fault = 8;
            bool success = !strcmp(name, "scan-success") || !strcmp(name, "scan-slow-success");
            CHECK(encrypted_command(request, strlen(request), 7, 1) == success);
            CHECK(scan_results == (success ? 1u : 0u));
            if (success) {
                CHECK(hb_muse_pairing_status().phase == HB_MUSE_PAIRING_CONFIRMED);
                CHECK(encrypted_command(credentials, strlen(credentials), live_now+8, 2)); CHECK(stored == 1);
            } else CHECK(!stored);
            if (!strcmp(name, "scan-early") || !strcmp(name, "scan-extra")) CHECK(!scan_calls);
        }
    } else if (!strcmp(name, "expire-during-store")) {
        confirm(); expire_store = true;
        CHECK(!encrypted_command(credentials, strlen(credentials), 7, 1));
        CHECK(stored == 1 && !commits && hb_muse_pairing_status().phase == HB_MUSE_PAIRING_FAILED);
    } else if (!strcmp(name, "expire-during-send")) {
        CHECK(receive(hello, 2)); expire_send = true;
        CHECK(!encrypted_command(finished, strlen(finished), 3, 0));
        CHECK(!arms && !confirmations && hb_muse_pairing_status().phase == HB_MUSE_PAIRING_FAILED);
    } else if (!strcmp(name, "success")) {
        CHECK(receive("{\"action\":\"get_device_info\"}", 1)); CHECK(hello_calls == 0);
        confirm(); CHECK(encrypted_command(credentials, strlen(credentials), 7, 1));
        CHECK(stored == 1 && hb_muse_pairing_status().phase == HB_MUSE_PAIRING_STORED);
        CHECK(completion_stage == 4);
    } else if (!strncmp(name, "legacy-api-", 11)) {
        confirm(); cJSON *root=cJSON_Parse(credentials); CHECK(root);
        char large[2049]; memset(large,'A',sizeof(large));large[2048]=0;
        if (!strcmp(name,"legacy-api-type")) cJSON_AddNumberToObject(root,"api_url",7);
        else cJSON_AddStringToObject(root,"api_url",!strcmp(name,"legacy-api-control")?"https://fixture.invalid/\r\n":
            !strcmp(name,"legacy-api-oversize")?large:"https://not-used.invalid/legacy");
        char *json=cJSON_PrintUnformatted(root);CHECK(json);cJSON_Delete(root);
        bool allowed=!strcmp(name,"legacy-api-ignored");
        CHECK(encrypted_command(json,strlen(json),7,1)==allowed);
        CHECK(stored==(allowed?1u:0u));free(json);
    } else if (!strncmp(name, "refresh-", 8)) {
        confirm(); char refresh[HB_MUSE_PAIRING_REFRESH_CAP + 1u];
        strcpy(refresh,"hatch_refresh:fixture|opaque%value!\"\\");
        bool allowed = !strcmp(name,"refresh-opaque") || !strcmp(name,"refresh-long") || !strcmp(name,"refresh-limit");
        if (!strcmp(name,"refresh-long")) { memset(refresh,'R',1500); refresh[1500]=0; }
        if (!strcmp(name,"refresh-limit")) { size_t n=HB_MUSE_PAIRING_REFRESH_CAP-sizeof("Bearer hatch_refresh:"); memset(refresh,'R',n);refresh[n]=0; }
        if (!strcmp(name,"refresh-overflow")) { memset(refresh,'R',HB_MUSE_PAIRING_REFRESH_CAP);refresh[HB_MUSE_PAIRING_REFRESH_CAP]=0; }
        if (!strcmp(name,"refresh-control")) strcpy(refresh,"fixture\r\nInjected:x");
        if (!strcmp(name,"refresh-space")) strcpy(refresh,"fixture space");
        if (!strcmp(name,"refresh-empty-suffix")) strcpy(refresh,"hatch_refresh:");
        cJSON *root=cJSON_Parse(credentials); CHECK(root);
        cJSON_ReplaceItemInObject(root,"refresh_token",cJSON_CreateString(refresh));
        char *json=cJSON_PrintUnformatted(root);CHECK(json);cJSON_Delete(root);
        CHECK(strlen(json)<=3072u); expected_refresh=refresh;
        CHECK(encrypted_command(json,strlen(json),7,1)==allowed);
        CHECK(stored==(allowed?1u:0u)); expected_refresh=NULL;free(json);
    } else if (!strcmp(name, "maximum-tokens") || !strcmp(name, "oversize-token")) {
        confirm(); maximum_tokens = !strcmp(name, "maximum-tokens");
        char access[HB_MUSE_PAIRING_TOKEN_CAP + 1u], refresh[HB_MUSE_PAIRING_TOKEN_CAP];
        memset(access, 'A', sizeof(access)); access[maximum_tokens ? HB_MUSE_PAIRING_TOKEN_CAP - 1u : HB_MUSE_PAIRING_TOKEN_CAP] = 0;
        memset(refresh, 'B', sizeof(refresh)); refresh[sizeof(refresh) - 1u] = 0;
        cJSON *root = cJSON_Parse(credentials); CHECK(root);
        cJSON_ReplaceItemInObject(root, "access_token", cJSON_CreateString(access));
        cJSON_ReplaceItemInObject(root, "refresh_token", cJSON_CreateString(refresh));
        char *json = cJSON_PrintUnformatted(root); CHECK(json); cJSON_Delete(root);
        CHECK(strlen(json) <= 3072u);
        CHECK(encrypted_command(json, strlen(json), 7, 1) == maximum_tokens);
        CHECK(stored == (maximum_tokens ? 1u : 0u)); free(json);
    } else if (!strcmp(name, "padded-cipher") || !strcmp(name, "cipher-whitespace") || !strcmp(name, "cipher-too-large")) {
        CHECK(receive(hello, 2)); cJSON *root = cJSON_CreateObject();
        char large[4098]; memset(large, 'A', sizeof(large)); large[sizeof(large) - 1u] = 0;
        cJSON_AddStringToObject(root, "action", "pairing_encrypted");
        cJSON_AddStringToObject(root, "ciphertext", !strcmp(name, "padded-cipher") ? "AAA=" :
            !strcmp(name, "cipher-whitespace") ? "AAA " : large);
        char *json = cJSON_PrintUnformatted(root); CHECK(json); cJSON_Delete(root);
        CHECK(!receive(json, 3) && !decrypt_calls); free(json);
    } else if (!strcmp(name, "held-button")) {
        prompt(); uint64_t t = hb_muse_pairing_status().confirmation_ticket;
        CHECK(!hb_muse_pairing_button(t, 4, true, 5)); CHECK(!confirmations && !stored);
    } else if (!strcmp(name, "stale-ticket")) {
        prompt(); uint64_t t = hb_muse_pairing_status().confirmation_ticket;
        hb_muse_pairing_disconnect(1); CHECK(!hb_muse_pairing_button(t, 4, true, 4));
        CHECK(!hb_muse_pairing_connected(1, 4)); CHECK(hb_muse_pairing_connected(2, 4)); CHECK(!confirmations);
    } else if (!strcmp(name, "failed-prompt") || !strcmp(name, "disconnect-send")) {
        CHECK(receive(hello, 2)); notify_ok = strcmp(name, "failed-prompt") != 0;
        revoke_send = !strcmp(name, "disconnect-send");
        CHECK(!encrypted_command(finished, strlen(finished), 3, 0)); CHECK(!arms && !confirmations);
    } else if (!strcmp(name, "early-provision")) {
        prompt(); CHECK(!encrypted_command(credentials, strlen(credentials), 4, 1)); CHECK(!stored);
    } else if (!strcmp(name, "expiry") || !strcmp(name, "rollback") || !strcmp(name, "sdk-expiry")) {
        prompt(); if (!strcmp(name, "sdk-expiry")) expire_sdk = true;
        hb_muse_pairing_poll(!strcmp(name, "expiry") ? HB_MUSE_PAIRING_WINDOW_MS : !strcmp(name, "rollback") ? 1u : 4u);
        CHECK(hb_muse_pairing_status().phase == HB_MUSE_PAIRING_FAILED && !confirmations);
    } else if (!strcmp(name, "window-generation")) {
        hb_muse_pairing_revoke(); CHECK(!hb_muse_pairing_open(1, 2)); CHECK(hb_muse_pairing_open(2, 2));
        CHECK(!hb_muse_pairing_connected(1, 3)); CHECK(hb_muse_pairing_connected(2, 3));
    } else if (!strcmp(name, "duplicate-json") || !strcmp(name, "trailing-json") || !strcmp(name, "nul-json") ||
               !strcmp(name, "nested-json") || !strcmp(name, "plaintext-provision")) {
        const char *bad = !strcmp(name, "duplicate-json") ? "{\"action\":\"get_device_info\",\"action\":\"get_device_info\"}" :
            !strcmp(name, "trailing-json") ? "{\"action\":\"get_device_info\"}{}" :
            !strcmp(name, "nul-json") ? "{\"action\":\"get_device_info\\u0000junk\"}" :
            !strcmp(name, "nested-json") ? "{\"action\":{\"secret\":\"fixture\"}}" : credentials;
        CHECK(!receive(bad, 2)); CHECK(!stored && !hello_calls);
    } else if (!strcmp(name, "finished-extra") || !strcmp(name, "embedded-plain-nul")) {
        CHECK(receive(hello, 2));
        const char malformed[] = "{\"action\":\"pairing_client_finished\"}\0{}";
        const char *bad = !strcmp(name, "finished-extra") ? "{\"action\":\"pairing_client_finished\",\"extra\":1}" : malformed;
        size_t n = !strcmp(name, "finished-extra") ? strlen(bad) : sizeof(malformed) - 1u;
        CHECK(!encrypted_command(bad, n, 3, 0)); CHECK(!arms);
    } else if (!strcmp(name, "decrypt-fails") || !strcmp(name, "replay")) {
        confirm(); reject_crypto = !strcmp(name, "decrypt-fails");
        CHECK(!encrypted_command(credentials, strlen(credentials), 7, reject_crypto ? 1u : 0u)); CHECK(!stored);
    } else if (!strcmp(name, "api-override") || !strcmp(name, "noise-override") || !strcmp(name, "ota-blocked") ||
               !strcmp(name, "token-whitespace")) {
        confirm(); cJSON *root = cJSON_Parse(credentials); CHECK(root);
        if (!strcmp(name, "api-override")) cJSON_AddStringToObject(root, "api_url_v2", "https://fixture.invalid");
        if (!strcmp(name, "noise-override")) cJSON_AddStringToObject(root, "noise_host", "fixture.invalid");
        if (!strcmp(name, "ota-blocked")) cJSON_AddStringToObject(root, "ota_url", "https://fixture.invalid/image");
        if (!strcmp(name, "token-whitespace")) cJSON_ReplaceItemInObject(root, "access_token", cJSON_CreateString("bad\nfixture"));
        char *bad = cJSON_PrintUnformatted(root); CHECK(bad); cJSON_Delete(root);
        CHECK(!encrypted_command(bad, strlen(bad), 7, 1)); CHECK(!stored); free(bad);
    } else {
        CHECK(!strcmp(name, "no-provider") || !strcmp(name, "disconnect-store") || !strcmp(name, "store-fails"));
        confirm(); revoke_store = !strcmp(name, "disconnect-store"); persist_ok = strcmp(name, "store-fails") != 0;
        CHECK(!encrypted_command(credentials, strlen(credentials), 7, 1));
        CHECK(hb_muse_pairing_status().phase == HB_MUSE_PAIRING_FAILED);
    }
    hb_muse_pairing_revoke(); CHECK(hb_muse_pairing_status().phase == HB_MUSE_PAIRING_OFF);
    printf("PASS %s\n", name); return 0;
}
