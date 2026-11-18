target_sources(
    ${CORE_TARGET_NAME}
    PRIVATE database.cpp
            databasemanager.cpp
            databasetasks.cpp
            query_recorder.cpp
            schema_hardening.cpp
)
