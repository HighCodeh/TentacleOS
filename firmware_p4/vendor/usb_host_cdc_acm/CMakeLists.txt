# 1. IDF version >= 6.0 does not have usb component: usb from IDF component manager will be used
# 2. For linux target, we can't use IDF component manager to get usb component, we need to add it 'the old way'
#    with EXTRA_COMPONENT_DIRS because mocking of managed components is not supported yet.
#    This is acceptable workaround for testing.
set(requires "")
if((${IDF_VERSION_MAJOR} LESS 6) OR ("${IDF_TARGET}" STREQUAL "linux"))
    list(APPEND requires usb)
endif()

idf_component_register(SRCS
                        "cdc_acm_host.c"                # Contains driver and transfers code
                        "cdc_host_descriptor_parsing.c" # Only descriptor parsing code, no CDC device handling
                        "cdc_host_acm_compliant.c"      # Implementation of CDC ACM compliant functions
                        "cdc_host_ops.c"                # Implementation of CDC ACM host operations
                       INCLUDE_DIRS "include" "interface"
                       PRIV_INCLUDE_DIRS "private_include" "include/esp_private"
                       REQUIRES "${requires}"
                       )
