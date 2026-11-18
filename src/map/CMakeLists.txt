target_sources(
    ${CORE_TARGET_NAME}
    PRIVATE house/house.cpp
            house/house_auction_rules.cpp
            house/house_repository_db.cpp
            house/housetile.cpp
            utils/astarnodes.cpp
            utils/mapsector.cpp
            map.cpp
            navigation_snapshot.cpp
            navigation_snapshot.hpp
            map_download.cpp
            mapcache.cpp
            spectators.cpp
)
