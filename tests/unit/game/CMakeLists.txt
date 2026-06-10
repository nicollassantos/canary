target_sources(
    canary_ut
    PRIVATE bank_test.cpp
            events_scheduler_test.cpp
            highscores_test.cpp
            npc_shop_range_test.cpp
            random_mount_test.cpp
            player_stats_test.cpp
            spawn_monster_test.cpp
            combat_service_test.cpp
            item_service_test.cpp
            player_interaction_service_test.cpp
            zone_area_test.cpp
            teleport_guard_test.cpp
)
