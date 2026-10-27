// Daemon app: registers a console command (uses a writable global for the api),
// then runs until stopped. On exit the manager unregisters the command.
#include "tos_api.h"

static const tos_api_t *g_api;

static int svc_cmd(int argc, char **argv) {
  (void)argc;
  (void)argv;
  g_api->log(TOS_LOG_INFO, "SVC", "hello from an app-registered command!");
  return 0;
}

int app_main(const tos_api_t *api, int argc, char **argv) {
  (void)argc;
  (void)argv;
  g_api = api;
  api->console_register("svctest", "command provided by the svc app", svc_cmd);
  api->log(TOS_LOG_INFO, "SVC", "registered 'svctest'; running until appstop");
  while (!api->should_stop())
    api->delay_ms(200);
  api->log(TOS_LOG_INFO, "SVC", "stopping; 'svctest' will be removed");
  return 0;
}
