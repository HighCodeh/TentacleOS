// ===========================================================================
// HID layouts equivalence test (host-side, no hardware).
//
// Proves the table-driven refactor of hid_layouts.c is behavior-preserving:
// for every input it asserts the NEW hid_layouts_type_string() emits exactly
// the same hid_hal_press_key(keycode, modifier) sequence as the frozen,
// pre-refactor functions hid_layouts_type_string_us()/_abnt2().
//
// Build & run: see run_equiv.sh / run_equiv.ps1 in this directory.
// Exit code 0 = all equivalent; 1 = at least one mismatch.
// ===========================================================================

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hid_layouts.h" // new + legacy prototypes, ducky_layout_t

// --- press log: our stub for hid_hal_press_key() ---------------------------
#define LOG_CAP 8192
static uint8_t g_kc[LOG_CAP];
static uint8_t g_mod[LOG_CAP];
static int     g_n;

void hid_hal_press_key(uint8_t keycode, uint8_t modifiers) {
  if (g_n < LOG_CAP) {
    g_kc[g_n] = keycode;
    g_mod[g_n] = modifiers;
  }
  g_n++; // keep counting past LOG_CAP so length mismatches are still detected
}

typedef struct {
  int     n;
  uint8_t kc[LOG_CAP];
  uint8_t mod[LOG_CAP];
} press_log_t;

static void snapshot(press_log_t *out) {
  out->n = g_n;
  int n = g_n < LOG_CAP ? g_n : LOG_CAP;
  memcpy(out->kc, g_kc, (size_t)n);
  memcpy(out->mod, g_mod, (size_t)n);
}

static int logs_equal(const press_log_t *a, const press_log_t *b) {
  if (a->n != b->n) {
    return 0;
  }
  int n = a->n < LOG_CAP ? a->n : LOG_CAP;
  for (int i = 0; i < n; ++i) {
    if (a->kc[i] != b->kc[i] || a->mod[i] != b->mod[i]) {
      return 0;
    }
  }
  return 1;
}

static void print_hex(const char *s) {
  for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
    printf("%02X ", *p);
  }
}

static void print_log(const press_log_t *l) {
  int n = l->n < LOG_CAP ? l->n : LOG_CAP;
  for (int i = 0; i < n; ++i) {
    printf("(%02X,%02X) ", l->kc[i], l->mod[i]);
  }
  if (l->n > LOG_CAP) {
    printf("...[+%d truncated]", l->n - LOG_CAP);
  }
}

// --- comparison driver ------------------------------------------------------
typedef void (*legacy_fn)(const char *);

static int g_tests = 0;
static int g_fails = 0;

static void check(const char *label, ducky_layout_t layout, legacy_fn oracle,
                  const char *s) {
  press_log_t a, b;

  g_n = 0;
  oracle(s);
  snapshot(&a);

  g_n = 0;
  hid_layouts_type_string(layout, s);
  snapshot(&b);

  ++g_tests;
  if (!logs_equal(&a, &b)) {
    ++g_fails;
    printf("MISMATCH [%-5s] input bytes: ", label);
    print_hex(s);
    printf("\n  old(%d): ", a.n);
    print_log(&a);
    printf("\n  new(%d): ", b.n);
    print_log(&b);
    printf("\n");
  }
}

// Run one input against both layouts, each vs its legacy oracle.
static void check_both(const char *s) {
  check("US", DUCKY_LAYOUT_US, hid_layouts_type_string_us, s);
  check("ABNT2", DUCKY_LAYOUT_ABNT2, hid_layouts_type_string_abnt2, s);
}

int main(void) {
  char one[2] = {0, 0};
  char two[3] = {0, 0, 0};

  // 1) Every single byte 0x01..0xFF (covers all ASCII + stray high bytes).
  for (int b = 1; b < 256; ++b) {
    one[0] = (char)b;
    check_both(one);
  }

  // 2) Every 0xC3 0xXX pair (the ABNT2 UTF-8 accent lead), 0xXX = 0x01..0xFF.
  two[0] = (char)0xC3;
  for (int b = 1; b < 256; ++b) {
    two[1] = (char)b;
    check_both(two);
  }

  // 3) Other multibyte leads / malformed sequences (exercise fall-through).
  check_both("\xC3");             // lone lead at end of string
  check_both("\xC2\xA1");         // 0xC2 lead, not in the ABNT2 table
  check_both("\xC4\x80");         // 0xC4 lead
  check_both("\xE2\x82\xAC");     // 3-byte sequence
  check_both("\xF0\x9F\x98\x80"); // 4-byte sequence

  // 4) Curated strings (integration: chaining, skips, mixed content).
  check_both("");
  check_both("Hello, World!");
  check_both("The quick brown fox 0123456789");
  check_both("!@#$%^&*()-_=+[]{}\\|;:'\",.<>/?`~");
  check_both("user@example.com  C:\\Users\\test");
  check_both("acao coracao nao"); // ASCII-only baseline
  check_both("A" "\xC3\xA7" "\xC3\xA3" "o do cora" "\xC3\xA7" "\xC3\xA3" "o");
  check_both("'single' and \"double\" quotes");
  // Full ABNT2 accent showcase: c C / a e i o u / a e o / a o / a
  check_both("\xC3\xA7" "\xC3\x87" " "
             "\xC3\xA1" "\xC3\xA9" "\xC3\xAD" "\xC3\xB3" "\xC3\xBA" " "
             "\xC3\xA2" "\xC3\xAA" "\xC3\xB4" " "
             "\xC3\xA3" "\xC3\xB5" " "
             "\xC3\xA0");

  printf("\n==== HID layouts equivalence: %d cases, %d mismatch(es) ====\n",
         g_tests, g_fails);
  if (g_fails == 0) {
    printf("RESULT: PASS  (new table-driven output == frozen old output)\n");
  } else {
    printf("RESULT: FAIL  (%d mismatch(es) above)\n", g_fails);
  }
  return g_fails == 0 ? 0 : 1;
}
