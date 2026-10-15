#include <stdint.h>
#include <string.h>

#include "argtable3.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 3) {
        return 0;
    }

    // Prepare input data
    const char *input1 = (const char*)data;
    const char *input2 = (const char*)(data + 1);
    struct tm tm_value;

    // Ensure null-terminated strings
    char str1[size+1];
    char str2[size];
    memcpy(str1, input1, size);
    str1[size] = '\0';
    memcpy(str2, input2, size-1);
    str2[size-1] = '\0';

    // Call the target function
    arg_strptime(str1, str2, &tm_value);

    return 0;
}
