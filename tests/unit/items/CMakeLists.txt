target_sources(
    canary_ut
    PRIVATE containers/container_test.cpp
            containers/mailbox_receiver_test.cpp
            custom_attribute_test.cpp
            item_attribute_test.cpp
            item_creation_test.cpp
)
