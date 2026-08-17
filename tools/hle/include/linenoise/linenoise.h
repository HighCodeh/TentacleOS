#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef int linenoiseCompletions;
void linenoiseSetCompletionCallback(void (*cb)(const char *, void *), void *ctx);
char *linenoise(const char *prompt);
void linenoiseHistoryLoad(const char *filename);
void linenoiseHistorySave(const char *filename);
void linenoiseHistoryAdd(const char *line);
void linenoiseFree(void *ptr);

#ifdef __cplusplus
}
#endif
