cmake_minimum_required(VERSION 3.25)  # FetchContent SYSTEM
# Unset, the oldest macOS a build runs on is the SDK's (26 on CI). Keep in
# step with LSMinimumSystemVersion (make-mac-app.sh, jm export).
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "The oldest macOS the binaries run on")
project(JourneymanEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(JM_BUILD_TESTS "Build unit tests" OFF)
option(JM_BUILD_EDITOR "Build the editor (journeyman_editor)" ON)
option(JM_WARNINGS_AS_ERRORS "Fail the build on a warning in our code (CI)" OFF)
set(JM_VERSION "dev" CACHE STRING "The release this build is (journeyman_engine --version), e.g. v0.0.2")

if(JM_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(vendor)

# Our code (everything after vendor/) builds with warnings on, less two
# idioms it uses on purpose: designated initializers that leave fields at
# their defaults, and overrides that ignore a parameter.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter)
  if(JM_WARNINGS_AS_ERRORS)
    add_compile_options(-Werror)
  endif()
endif()

if(JM_BUILD_TESTS)
  include(GoogleTest)
endif()

add_subdirectory(engine)

if(JM_BUILD_EDITOR)
  add_subdirectory(editor)
endif()
