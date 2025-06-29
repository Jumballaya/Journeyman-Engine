include(FetchContent)

# # glfw
# add_subdirectory(glfw)

# # glad
# add_library(glad STATIC glad/src/glad.c)
# target_include_directories(glad PUBLIC glad/include)

# # wasm3
# add_subdirectory(wasm3)

# json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)

FetchContent_MakeAvailable(nlohmann_json)