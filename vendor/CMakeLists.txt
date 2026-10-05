include(FetchContent)

# wasm3
set(WASM3_ENABLE_WASI OFF CACHE BOOL "Disable WASI support")
set(WASM3_ENABLE_LIBC OFF CACHE BOOL "Disable libc support")
set(WASM3_BUILD_WASI OFF CACHE BOOL "" FORCE)
set(WASM3_NO_MEMORY_INIT OFF CACHE BOOL "" FORCE)
set(WASM3_NO_COMPILER OFF CACHE BOOL "" FORCE)
set(WASM3_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
# wasm3 defaults to -mcpu/-march=native; exported games must run on CPUs
# older than the build machine.
set(BUILD_NATIVE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  wasm3
  GIT_REPOSITORY https://github.com/wasm3/wasm3.git
  # Pinned: wasm3 main changed m3_GetMemory's signature (runtime -> module)
  # in late 2026; every host function uses the runtime-based API.
  GIT_TAG        79d412ea5fcf92f0efe658d52827a0e0a96ff442
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

# GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

###
# GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)
set(GLM_TEST_ENABLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

###
# stb image
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    # Pinned for reproducible builds (stb has no release tags).
    GIT_TAG 31c1ad37456438565541f4919958214b6e762fb4
)
FetchContent_MakeAvailable(stb)

add_library(stb_lib INTERFACE)
target_include_directories(stb_lib INTERFACE ${stb_SOURCE_DIR})

###
# Glad
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.8
)
FetchContent_MakeAvailable(glad)

list(APPEND CMAKE_MODULE_PATH ${glad_SOURCE_DIR}/cmake)
include(GladConfig)
if(APPLE)
  glad_add_library(glad_lib STATIC API gl:core=4.1 EXTENSIONS GL_ARB_texture_storage)
else()
  glad_add_library(glad_lib STATIC API gl:core=4.6)
endif()

###
# GoogleTest (only fetched when JM_BUILD_TESTS is ON)
if(JM_BUILD_TESTS)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.15.2
  )
  FetchContent_MakeAvailable(googletest)
endif()
###
# Editor only: Dear ImGui (docking branch) and native file dialogs
if(JM_BUILD_EDITOR)
  FetchContent_Declare(
      imgui
      GIT_REPOSITORY https://github.com/ocornut/imgui.git
      GIT_TAG v1.92.9b-docking
  )
  FetchContent_MakeAvailable(imgui)

  add_library(imgui_lib STATIC
      ${imgui_SOURCE_DIR}/imgui.cpp
      ${imgui_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_SOURCE_DIR}/imgui_widgets.cpp
      ${imgui_SOURCE_DIR}/imgui_demo.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
      ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  )
  target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
  target_compile_definitions(imgui_lib PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
  target_link_libraries(imgui_lib PUBLIC glfw)

  set(NFD_PORTAL OFF CACHE BOOL "" FORCE)
  set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
      nfd
      GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
      GIT_TAG v1.2.1
  )
  FetchContent_MakeAvailable(nfd)
endif()
