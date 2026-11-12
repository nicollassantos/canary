target_sources(
    canary_ut
    PRIVATE network/connection/connection_write_diagnostics_test.cpp
            network/message/networkmessage_test.cpp
            network/protocol/multiprotocol_test.cpp
            network/protocol/packet_watch_test.cpp
            network/connection/ip_connection_limiter_test.cpp
)
