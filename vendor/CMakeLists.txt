# glfw
add_subdirectory(glfw)

# glad
add_library(glad STATIC glad/src/glad.c)
target_include_directories(glad PUBLIC glad/include)

# wasm3
add_subdirectory(wasm3)

# json
add_subdirectory(nlohmann_json)