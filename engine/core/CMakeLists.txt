add_subdirectory(ecs)
add_subdirectory(async)
add_subdirectory(tasks)

add_library(engine_core INTERFACE)

target_link_libraries(engine_core
    INTERFACE
        engine_ecs
        engine_async
        engine_tasks
)

# target_link_libraries(engine_core
#   wasm3
#   nlohmann_json::nlohmann_json
# )