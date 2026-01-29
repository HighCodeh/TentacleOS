#include "console_service.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "cJSON.h"

// Argument structs
static struct {
    struct arg_str *path;
    struct arg_lit *json;
    struct arg_end *end;
} ls_args;

static struct {
    struct arg_str *path;
    struct arg_end *end;
} cat_args;

static int cmd_ls(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&ls_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, ls_args.end, argv[0]);
        return 1;
    }

    const char *path = (ls_args.path->count > 0) ? ls_args.path->sval[0] : "/assets";
    bool use_json = (ls_args.json->count > 0);

    DIR *dir = opendir(path);
    if (!dir) {
        printf("Error: Cannot open directory '%s'\n", path);
        return 1;
    }

    struct dirent *entry;
    cJSON *root = use_json ? cJSON_CreateArray() : NULL;

    if (!use_json) printf("Listing directory: %s\n", path);

    while ((entry = readdir(dir)) != NULL) {
        char full_path[512];
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
        arg_print_errors(stderr, cat_args.end, argv[0]);
        return 1;
    }

    const char *path = cat_args.path->sval[0];
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("Error: Cannot open file '%s'\n", path);
        return 1;
    }

    char buf[128];
    while (fgets(buf, sizeof(buf), f) != NULL) {
        printf("%s", buf);
    }
    printf("\n"); // Ensure newline at end
    fclose(f);
    return 0;
}

void register_fs_commands(void) {
    ls_args.path = arg_str0(NULL, NULL, "<path>", "Directory path");
    ls_args.json = arg_lit0("j", "json", "Output in JSON format");
    ls_args.end = arg_end(1);

    const esp_console_cmd_t ls_cmd = {
        .command = "ls",
        .help = "List directory content",
        .hint = NULL,
        .func = &cmd_ls,
        .argtable = &ls_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ls_cmd));

    cat_args.path = arg_str1(NULL, NULL, "<file>", "File path");
    cat_args.end = arg_end(1);

    const esp_console_cmd_t cat_cmd = {
        .command = "cat",
        .help = "Print file content",
        .hint = NULL,
        .func = &cmd_cat,
        .argtable = &cat_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cat_cmd));
}
