# Testing mDNS sender, need to mock pbb
set(MOCK_FILES
        "mdns_pcb"
)

list(APPEND SOURCES unity/${UNIT_TESTS}/test_sender.c)
