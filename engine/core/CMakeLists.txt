add_subdirectory(ecs)
add_subdirectory(assets)
add_subdirectory(async)
add_subdirectory(tasks)
add_subdirectory(scripting)

add_library(engine_core INTERFACE)
target_link_libraries(engine_core
    PUBLIC
        engine_ecs
        engine_assets
        engine_async
        engine_tasks
        engine_scripting
)

target_link_libraries(engine_core
  glad
  glfw
  wasm3
  nlohmann_json::nlohmann_json
)