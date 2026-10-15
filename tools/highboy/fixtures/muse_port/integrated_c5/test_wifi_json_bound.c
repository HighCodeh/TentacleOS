#include "hb_muse_wifi_json_bound.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool check(const char *s) { return hb_muse_wifi_json_shallow((const uint8_t*)s,strlen(s)); }
int main(void) {
    assert(check("[]"));
    assert(check("[{\"ssid\":\"fixture\",\"password\":\"fixture-only\"}]"));
    assert(check(" \n[{\"ssid\":\"[{}]\",\"password\":\"\\\"\\\\\"}]\n"));
    assert(check("[{\"ssid\":\"literal\\\\u0000\",\"password\":\"p\"}]"));
    assert(!check("[{\"ssid\":\"fixture\",\"password\":\"p\",\"nested\":[{}]}]"));
    assert(!check("[[[[]]]]"));
    assert(!check("[{\"ssid\":\"fixture\\u0000suffix\",\"password\":\"p\"}]"));
    assert(!check("[{\"ssid\":\"unfinished"));
    assert(!check("[{\"ssid\":\"unfinished\\"));
    assert(!check("[{\"ssid\":\"bad\nnewline\"}]"));
    assert(!check("[{\"ssid\":\"s\"}"));
    assert(!check("]"));
    assert(!hb_muse_wifi_json_shallow((const uint8_t*)"[]\0[]",5));
    assert(!hb_muse_wifi_json_shallow(NULL,1));
    assert(!hb_muse_wifi_json_shallow((const uint8_t*)"",0));
    uint8_t deep[32769];memset(deep,'[',sizeof(deep));
    assert(!hb_muse_wifi_json_shallow(deep,sizeof(deep)));
    assert(!hb_muse_wifi_json_shallow(deep,sizeof(deep)-1));
    puts("PASS 17 bounded Wi-Fi JSON prescan cases; no parser/store/device");
    return 0;
}
