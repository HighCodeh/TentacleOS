// Exercises the per-app memory arena: allocates, frees one block, and leaks
// another on purpose. The manager should reclaim the leak when the app exits.
#include "tos_api.h"

int app_main(const tos_api_t *api, int argc, char **argv) {
  (void)argc;
  (void)argv;
  void *a = api->mem_alloc(4096);
  void *b = api->mem_alloc(8192);
  if (a != 0 && b != 0)
    api->log(TOS_LOG_INFO, "ALLOC", "allocated 4K + 8K from the arena");
  else
    api->log(TOS_LOG_ERROR, "ALLOC", "allocation failed");
  api->mem_free(a); // free one, leak the other on purpose
  api->log(TOS_LOG_INFO, "ALLOC", "freed 4K, leaking 8K (manager reclaims on exit)");
  return 0;
}
