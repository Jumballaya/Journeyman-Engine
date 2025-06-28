cmake_minimum_required(VERSION 3.20)
project(JourneymanEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
  add_compile_options(/W4 /Wx)
else()
  add_compile_options(-Wall -Wextra -Wpendantic -Werror)
endif()

add_subdirectory(vendor)
add_subdirectory(engine)