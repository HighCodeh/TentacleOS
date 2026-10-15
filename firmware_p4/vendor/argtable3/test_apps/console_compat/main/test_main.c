#include "esp_console.h"

void app_main(void)
{
    // call a function of console component which uses argtable3
    esp_console_register_help_command();

    // in CMakeLists.txt, we'll check the map file to make sure
    // that the correct version of argtable3 functions was linked.
}
