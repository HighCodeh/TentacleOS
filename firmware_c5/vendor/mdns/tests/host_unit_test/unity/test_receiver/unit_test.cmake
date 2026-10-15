# Testing mDNS receiver, so mocking pbb and send
set(MOCK_FILES
        "mdns_pcb"
        "mdns_send"
)

list(APPEND SOURCES unity/${UNIT_TESTS}/test_receiver.c)
