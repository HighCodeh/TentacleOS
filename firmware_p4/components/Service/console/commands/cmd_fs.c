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


#include "console_service.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "cJSON.h"

// Global CWD
static char cwd[512] = "/assets";

// Helper to resolve path (absolute or relative to cwd)
static void resolve_path(const char *input, char *output, size_t max_len) {
  if (input == NULL || strlen(input) == 0) {
    strncpy(output, cwd, max_len);
    return;
  }

  if (input[0] == '/') {
    // Absolute path
    strncpy(output, input, max_len);
  } else {
    // Relative path
    snprintf(output, max_len, "%s/%s", cwd, input);
  }

  // Basic normalization could be added here (handling ..), 
  // but standard VFS might not support complex normalization automatically.
  // For now, let's keep it simple or implement manual .. handling if needed.
  // Simple manual .. handling:
  // This is a naive implementation.
  // If input is "..", we strip last component from cwd (if not absolute input).
}

// LS Args
static struct {
  struct arg_str *path;
  struct arg_lit *json;
  struct arg_end *end;
} ls_args;

// CD Args
static struct {
  struct arg_str *path;
  struct arg_end *end;
} cd_args;

// CAT Args
static struct {
  struct arg_str *path;
  struct arg_end *end;
} cat_args;

static int cmd_pwd(int argc, char **argv) {
  printf("%s\n", cwd);
  return 0;
}

static int cmd_cd(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&cd_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cd_args.end, "cd");
    printf("Usage: cd <path>\n");
    return 1;
  }
  const char *target = cd_args.path->sval[0];
  char new_path[512];

  // Handle special case ".." manually because typical MCU VFS is dumb
  if (strcmp(target, "..") == 0) {
    strncpy(new_path, cwd, sizeof(new_path));
    char *last_slash = strrchr(new_path, '/');
    if (last_slash && last_slash != new_path) {
      *last_slash = '\0'; // Truncate
    } else if (last_slash == new_path) {
      new_path[1] = '\0'; // Root /
    }
  } 
  else if (strcmp(target, ".") == 0) {
    return 0; 
  }
  else {
    resolve_path(target, new_path, sizeof(new_path));
  }

  // Verify if directory exists
  struct stat st;
  if (stat(new_path, &st) == 0 && S_ISDIR(st.st_mode)) {
    strncpy(cwd, new_path, sizeof(cwd));
    // Remove trailing slash if present and not root, purely cosmetic
    size_t len = strlen(cwd);
    if (len > 1 && cwd[len-1] == '/') {
      cwd[len-1] = '\0';
    }
    // printf("CWD changed to: %s\n", cwd);
  } else {
    printf("Error: Not a directory or path does not exist: %s\n", new_path);
    return 1;
  }
  return 0;
}

static int cmd_ls(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&ls_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, ls_args.end, "ls");
    printf("Usage: ls [-j] [path]\n");
    return 1;
  }
  char path[512];
  const char *input_path = (ls_args.path->count > 0) ? ls_args.path->sval[0] : NULL;
  resolve_path(input_path, path, sizeof(path));

  bool use_json = (ls_args.json->count > 0);

  DIR *dir = opendir(path);
  if (!dir) {
    printf("Error: Cannot open directory '%s'\n", path);
    return 1;
  }

  struct dirent *entry;
  cJSON *root = use_json ? cJSON_CreateArray() : NULL;

  if (!use_json) printf("Directory: %s\n", path);

  while ((entry = readdir(dir)) != NULL) {
    char full_path[1024]; // Increased buffer
    snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

    struct stat st;
    stat(full_path, &st);

    if (use_json) {
      cJSON *item = cJSON_CreateObject();
      cJSON_AddStringToObject(item, "name", entry->d_name);
      cJSON_AddStringToObject(item, "type", (entry->d_type == DT_DIR) ? "dir" : "file");
      cJSON_AddNumberToObject(item, "size", (double)st.st_size);
      cJSON_AddItemToArray(root, item);
    } else {
      printf("%-20s %s (%ld bytes)\n", entry->d_name, (entry->d_type == DT_DIR) ? "[DIR]" : "", (long)st.st_size);
    }
  }
  closedir(dir);

  if (use_json) {
    char *json_str = cJSON_PrintUnformatted(root);
    printf("%s\n", json_str);
    free(json_str);
    cJSON_Delete(root);
  }

  return 0;
}

static int cmd_cat(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&cat_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cat_args.end, "cat");
    printf("Usage: cat <file>\n");
    return 1;
  }
  char path[512];
  resolve_path(cat_args.path->sval[0], path, sizeof(path));

  FILE *f = fopen(path, "r");
  if (!f) {
    printf("Error: Cannot open file '%s'\n", path);
    return 1;
  }

  char buf[128];
  while (fgets(buf, sizeof(buf), f) != NULL) {
    printf("%s", buf);
  }
  printf("\n");
  fclose(f);
  return 0;
}

void register_fs_commands(void) {
  // LS
  ls_args.path = arg_str0(NULL, NULL, "<path>", "Directory path");
  ls_args.json = arg_lit0("j", "json", "Output in JSON format");
  ls_args.end = arg_end(1);
  const esp_console_cmd_t ls_cmd = { .command = "ls", .help = "List directory", .func = &cmd_ls, .argtable = &ls_args };
  ESP_ERROR_CHECK(esp_console_cmd_register(&ls_cmd));

  // CD
  cd_args.path = arg_str1(NULL, NULL, "<path>", "Target directory");
  cd_args.end = arg_end(1);
  const esp_console_cmd_t cd_cmd = { .command = "cd", .help = "Change directory", .func = &cmd_cd, .argtable = &cd_args };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cd_cmd));

  // PWD
  const esp_console_cmd_t pwd_cmd = { .command = "pwd", .help = "Print working directory", .func = &cmd_pwd };
  ESP_ERROR_CHECK(esp_console_cmd_register(&pwd_cmd));

  // CAT
  cat_args.path = arg_str1(NULL, NULL, "<file>", "File path");
  cat_args.end = arg_end(1);
  const esp_console_cmd_t cat_cmd = { .command = "cat", .help = "Print file content", .func = &cmd_cat, .argtable = &cat_args };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cat_cmd));
}
