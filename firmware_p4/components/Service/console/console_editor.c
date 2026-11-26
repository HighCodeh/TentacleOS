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

#include "console_editor.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_console.h"
#include "esp_log.h"
#include "linenoise/linenoise.h"

static const char *TAG = "CONSOLE_EDITOR";
#define LINE_CAPACITY    512
#define COMPLETION_LIMIT 32
#define ENTRY_LIMIT      256
#define HINT_COLOR       90

typedef struct {
  const char *prefix;
  const char *words;
} console_editor_options_t;
static const console_editor_options_t OPTIONS[] = {
    {"c5 ",
     "status diagnostics ping linktest spiclock info sync wait reboot boot ota rom passthrough "
     "release"},
    {"c5 spiclock ", "2 4 10"},
    {"c5 diagnostics ", "clear"},
    {"battery ", "regs profiles profile charge"},
    {"battery charge ", "on off"},
    {"battery profile ", "generic_2450 generic_2000 knlypow_604858"},
    {"wifi ", "scan connect ap config spam deauth sniff probe clients target"},
    {"hostlink ", "psk regen status ble"},
    {"hostlink ble ", "on off"},
    {"led ", "status color clear blink signal"},
    {"sd ", "status toexfat"},
    {"ir ", "rx send flash"},
    {"ir flash ", "on off"},
    {"badusb ", "run type layout stop status"},
    {"p4 ", "boot"}};

static void add_words(const char *line, linenoiseCompletions *out) {
  char candidate[LINE_CAPACITY];
  for (size_t i = 0; i < sizeof(OPTIONS) / sizeof(OPTIONS[0]); ++i) {
    size_t prefix = strlen(OPTIONS[i].prefix);
    if (strncmp(line, OPTIONS[i].prefix, prefix) != 0)
      continue;
    const char *partial = line + prefix;
    if (strchr(partial, ' ') != NULL)
      continue;
    const char *word = OPTIONS[i].words;
    while (*word != 0 && out->len < COMPLETION_LIMIT) {
      size_t length = strcspn(word, " ");
      if (strlen(partial) <= length && strncmp(word, partial, strlen(partial)) == 0) {
        int n =
            snprintf(candidate, sizeof(candidate), "%s%.*s", OPTIONS[i].prefix, (int)length, word);
        if (n > 0 && n < sizeof(candidate))
          linenoiseAddCompletion(out, candidate);
      }
      word += length;
      if (*word == ' ')
        ++word;
    }
  }
}

static void add_paths(const char *line, linenoiseCompletions *out) {
  const char *token = strchr(line, ' ');
  if (token == NULL)
    return;
  size_t command = token - line;
  if (!((command == 2 && (!strncmp(line, "ls", command) || !strncmp(line, "cd", command))) ||
        (command == 3 && !strncmp(line, "cat", command)) ||
        (command == 6 && !strncmp(line, "apprun", command))))
    return;
  while (*token == ' ')
    ++token;
  /* Quoted paths and options stay with the command parser. */
  if (strpbrk(token, "\"\\ ") != NULL || *token == '-')
    return;
  const char *slash = strrchr(token, '/');
  size_t directory_length = slash ? (size_t)(slash - token + 1) : 0;
  char directory[LINE_CAPACITY], candidate[LINE_CAPACITY], full[LINE_CAPACITY];
  int n = snprintf(directory,
                   sizeof(directory),
                   "%s%s%.*s",
                   *token == '/' ? "" : console_editor_cwd(),
                   *token == '/' ? "" : "/",
                   (int)directory_length,
                   token);
  if (n < 0 || n >= sizeof(directory))
    return;
  DIR *dir = opendir(directory);
  if (dir == NULL)
    return;
  const char *partial = token + directory_length;
  struct dirent *entry;
  unsigned visited = 0;
  while (out->len < COMPLETION_LIMIT && visited++ < ENTRY_LIMIT && (entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.' || strncmp(entry->d_name, partial, strlen(partial)) != 0 ||
        strpbrk(entry->d_name, " \"\\\r\n\t") != NULL)
      continue;
    n = snprintf(full, sizeof(full), "%s%s", directory, entry->d_name);
    struct stat st;
    if (n < 0 || n >= sizeof(full) || stat(full, &st) != 0)
      continue;
    if (command == 2 && !strncmp(line, "cd", command) && !S_ISDIR(st.st_mode))
      continue;
    n = snprintf(candidate,
                 sizeof(candidate),
                 "%.*s%.*s%s%s",
                 (int)(token - line),
                 line,
                 (int)directory_length,
                 token,
                 entry->d_name,
                 S_ISDIR(st.st_mode) ? "/" : "");
    if (n > 0 && n < sizeof(candidate))
      linenoiseAddCompletion(out, candidate);
  }
  closedir(dir);
}

static void complete(const char *line, linenoiseCompletions *out) {
  if (strchr(line, ' ') == NULL)
    esp_console_get_completion(line, out);
  else {
    add_words(line, out);
    add_paths(line, out);
  }
}

static char *hint(const char *line, int *color, int *bold) {
  static char suffix[LINE_CAPACITY];
  linenoiseCompletions options = {0};
  /* Avoid directory I/O on every keystroke; paths are expanded only by Tab. */
  if (*line != 0) {
    if (strchr(line, ' ') == NULL)
      esp_console_get_completion(line, &options);
    else
      add_words(line, &options);
  }
  suffix[0] = 0;
  if (options.len == 1 && strlen(options.cvec[0]) > strlen(line))
    snprintf(suffix, sizeof(suffix), "%s", options.cvec[0] + strlen(line));
  for (size_t i = 0; i < options.len; ++i)
    free(options.cvec[i]);
  free(options.cvec);
  *color = HINT_COLOR;
  *bold = 0;
  if (suffix[0] != 0)
    return suffix;
  const char *usage = esp_console_get_hint(line, color, bold);
  *color = HINT_COLOR;
  *bold = 0;
  if (usage == NULL)
    return NULL;
  snprintf(suffix, sizeof(suffix), "%s", usage);
  return suffix;
}

void console_editor_init(void) {
  linenoiseSetCompletionCallback(complete);
  linenoiseSetHintsCallback(hint);
  ESP_LOGD(TAG, "Completion and hints ready");
}
