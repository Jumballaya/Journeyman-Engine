include(FetchContent)

# # glfw
# add_subdirectory(glfw)

# # glad
# add_library(glad STATIC glad/src/glad.c)
# target_include_directories(glad PUBLIC glad/include)

# wasm3
set(WASM3_ENABLE_WASI OFF CACHE BOOL "Disable WASI support")
set(WASM3_ENABLE_LIBC OFF CACHE BOOL "Disable libc support")
set(WASM3_BUILD_WASI OFF CACHE BOOL "" FORCE)
set(WASM3_NO_MEMORY_INIT OFF CACHE BOOL "" FORCE)
set(WASM3_NO_COMPILER OFF CACHE BOOL "" FORCE)
set(WASM3_BUILD_TOOLS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  wasm3
  GIT_REPOSITORY https://github.com/wasm3/wasm3.git
  GIT_TAG        main
)

FetchContent_MakeAvailable(wasm3)

# nlohmann_json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)

FetchContent_MakeAvailable(nlohmann_json)

# miniaudio
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.22
)

FetchContent_MakeAvailable(miniaudio)

# spdlog
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.15.3
)

FetchContent_MakeAvailable(spdlog)