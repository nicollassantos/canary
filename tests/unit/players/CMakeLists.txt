target_sources(
    canary_ut
    PRIVATE cyclopedia_history_test.cpp
            forge_history_save_test.cpp
            player_item_batch_test.cpp
            player_inventory_test.cpp
            player_repository_test.cpp
            player_stats_behavior_test.cpp
            reward_iteration_test.cpp
            team_finder_rules_test.cpp
            title_leader_cache_test.cpp
)

add_subdirectory(components)
add_subdirectory(condition)
add_subdirectory(imbuements)

target_sources(
    canary_ut
    PRIVATE forge_test.cpp
            party_test.cpp
            ip_throttle_prune_test.cpp
)
