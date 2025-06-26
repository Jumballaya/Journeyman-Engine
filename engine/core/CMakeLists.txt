add_library(engine_core STATIC
)

target_include_directories(engine_core PUBLIC include)

target_link_libraries(engine_core
  glad
  glfw
  wasm3
  nlohmann_json::nlohmann_json
)