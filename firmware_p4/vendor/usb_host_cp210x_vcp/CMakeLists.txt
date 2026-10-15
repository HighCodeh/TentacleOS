idf_component_register(SRCS "usb_host_cp210x_vcp.c"
                    INCLUDE_DIRS "include"
                    REQUIRES usb_host_cdc_acm)
