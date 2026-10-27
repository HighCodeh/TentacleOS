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

#include "console_service.h"

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_err.h"
#include "esp_heap_caps.h"

#include "tos_api.h"
#include "tos_app_mgr.h"
#include "tos_grants.h"
#include "tos_hb.h"

#define APP_MAX_HB_SIZE (512 * 1024)
#define APPS_DIR        "/sdcard/apps"

static void print_caps(uint32_t caps) {
  static const struct {
    uint32_t bit;
    const char *name;
  } kNames[] = {
      {TOS_CAP_FS_READ, "fs-read"},   {TOS_CAP_FS_WRITE, "fs-write"},
      {TOS_CAP_RADIO_TX, "radio-tx"}, {TOS_CAP_RADIO_RX, "radio-rx"},
      {TOS_CAP_HID, "hid"},           {TOS_CAP_UI, "ui"},
      {TOS_CAP_HOSTLINK, "hostlink"}, {TOS_CAP_CONSOLE, "console"},
  };
  bool any = false;
  for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
    if (caps & kNames[i].bit) {
      printf("%s%s", any ? " " : "", kNames[i].name);
      any = true;
    }
  }
  if (!any)
    printf("(none)");
}

static int cmd_apprun(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: apprun <name>|/path/to.hb [grant]   (apps live in %s)\n", APPS_DIR);
    return 1;
  }

  // A bare name resolves to /sdcard/apps/<name>.hb; a path is used verbatim.
  char pathbuf[96];
  const char *path = argv[1];
  if (argv[1][0] != '/') {
    snprintf(pathbuf, sizeof(pathbuf), APPS_DIR "/%s.hb", argv[1]);
    path = pathbuf;
  }

  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    printf("apprun: cannot open %s\n", path);
    return 1;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > APP_MAX_HB_SIZE) {
    fclose(f);
    printf("apprun: bad file size %ld\n", sz);
    return 1;
  }
  uint8_t *heapbuf = heap_caps_malloc((size_t)sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (heapbuf == NULL) {
    fclose(f);
    printf("apprun: out of memory\n");
    return 1;
  }
  size_t rd = fread(heapbuf, 1, (size_t)sz, f);
  fclose(f);
  if (rd != (size_t)sz) {
    heap_caps_free(heapbuf);
    printf("apprun: short read\n");
    return 1;
  }
  const uint8_t *buf = heapbuf;
  size_t len = (size_t)sz;

  // Consent gate: an app runs only with capabilities the user approved. Peek the
  // manifest (this also verifies the signature) and compare against stored grants.
  tos_hb_t meta;
  esp_err_t e = tos_hb_open(buf, len, &meta);
  if (e == ESP_ERR_INVALID_CRC) {
    if (heapbuf != NULL)
      heap_caps_free(heapbuf);
    printf("apprun: rejected (bad or untrusted signature)\n");
    return 1;
  }
  if (e != ESP_OK) {
    if (heapbuf != NULL)
      heap_caps_free(heapbuf);
    printf("apprun: bad bundle: %s\n", esp_err_to_name(e));
    return 1;
  }
  uint32_t missing = meta.caps & ~tos_grants_get(meta.name);
  bool do_grant = (argc >= 3 && strcmp(argv[2], "grant") == 0);
  if (missing != 0 && !do_grant) {
    printf("apprun: '%s' requests capabilities you have not approved:\n  ", meta.name);
    print_caps(missing);
    printf("\n  approve and run with:  apprun %s grant\n", (argc >= 2) ? argv[1] : "hello");
    if (heapbuf != NULL)
      heap_caps_free(heapbuf);
    return 1;
  }
  if (missing != 0) {
    tos_grants_set(meta.name, meta.caps);
    printf("apprun: granted to '%s': ", meta.name);
    print_caps(meta.caps);
    printf("\n");
  }

  // The manager copies the bundle, so the read buffer can be freed right away.
  e = tos_app_mgr_start(buf, len, tos_api_get());
  if (heapbuf != NULL)
    heap_caps_free(heapbuf);

  if (e == ESP_ERR_INVALID_STATE) {
    printf("apprun: app limit reached (%d max); appstop one first\n", TOS_APP_MAX);
    return 1;
  }
  if (e == ESP_ERR_INVALID_CRC) {
    printf("apprun: rejected (bad or untrusted signature)\n");
    return 1;
  }
  if (e != ESP_OK) {
    printf("apprun: start failed: %s\n", esp_err_to_name(e));
    return 1;
  }
  printf("apprun: started\n");
  return 0;
}

static int cmd_appstop(int argc, char **argv) {
  char names[TOS_APP_MAX][TOS_APP_NAME_CAP];
  int n = tos_app_mgr_list(names, TOS_APP_MAX);

  const char *target = NULL;
  if (argc >= 2) {
    target = argv[1];
  } else if (n == 0) {
    printf("appstop: no app running\n");
    return 0;
  } else if (n == 1) {
    target = names[0];
  } else {
    printf("appstop: %d apps running, name one:", n);
    for (int i = 0; i < n; i++)
      printf(" %s", names[i]);
    printf("\n");
    return 1;
  }

  esp_err_t e = tos_app_mgr_stop(target, 2000); // teardown runs as the app exits
  if (e == ESP_ERR_NOT_FOUND) {
    printf("appstop: no app named '%s'\n", target);
    return 1;
  }
  printf("appstop: stopped '%s'\n", target);
  return 0;
}

static int cmd_apps(int argc, char **argv) {
  (void)argc;
  (void)argv;
  char names[TOS_APP_MAX][TOS_APP_NAME_CAP];
  int n = tos_app_mgr_list(names, TOS_APP_MAX);
  if (n == 0) {
    printf("no app running\n");
    return 0;
  }
  printf("%d app(s) running:", n);
  for (int i = 0; i < n; i++)
    printf(" %s", names[i]);
  printf("\n");
  return 0;
}

static int cmd_appgrant(int argc, char **argv) {
  if (argc >= 3 && strcmp(argv[2], "revoke") == 0) {
    tos_grants_clear(argv[1]);
    printf("appgrant: revoked '%s'\n", argv[1]);
    return 0;
  }
  char names[16][16];
  uint32_t caps[16];
  int n = tos_grants_list(names, caps, 16);
  if (n == 0) {
    printf("no capability grants stored\n");
    return 0;
  }
  printf("%d grant(s):\n", n);
  for (int i = 0; i < n; i++) {
    printf("  %s: ", names[i]);
    print_caps(caps[i]);
    printf("\n");
  }
  return 0;
}

void register_apprun_commands(void) {
  const esp_console_cmd_t run = {.command = "apprun",
                                 .help = "Run a .hb app from /sdcard/apps: apprun <name>|/path.hb [grant]",
                                 .hint = "<name>|/path.hb [grant]",
                                 .func = &cmd_apprun};
  const esp_console_cmd_t stop = {.command = "appstop",
                                  .help = "Stop a running app: appstop [name]",
                                  .hint = "[name]",
                                  .func = &cmd_appstop};
  const esp_console_cmd_t list = {
      .command = "apps", .help = "List running apps", .func = &cmd_apps};
  const esp_console_cmd_t grant = {.command = "appgrant",
                                   .help = "List capability grants, or: appgrant <name> revoke",
                                   .hint = "[name revoke]",
                                   .func = &cmd_appgrant};
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&run));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&stop));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&list));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&grant));
}
