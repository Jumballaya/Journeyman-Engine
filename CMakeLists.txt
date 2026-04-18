cmake_minimum_required(VERSION 3.20)
project(JourneymanEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(JM_BUILD_TESTS "Build unit tests" OFF)

if(JM_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(vendor)

if(JM_BUILD_TESTS)
  include(GoogleTest)
endif()

add_subdirectory(engine)