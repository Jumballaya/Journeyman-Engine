cmake_minimum_required(VERSION 3.20)
project(JourneymanEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
  add_compile_options(/W4 /Wx)
else()
  add_compile_options(-Wall -Wextra -Wpendantic -Werror)
endif()

# Dependencies
add_subdirectory(vendor)

# Engine modules
add_subdirectory(engine/core)
add_subdirectory(engine/app)
add_subdirectory(engine/renderer2d)
add_subdirectory(engine/audio)
