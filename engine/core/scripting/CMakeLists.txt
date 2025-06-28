file(GLOB_RECURSE SCRIPTING_SOURCES
    *.cpp
)

file(GLOB_RECURSE SCRIPTING_HEADERS
    *.hpp
)

add_library(engine_scripting ${SCRIPTING_SOURCES} ${APP_HEADERS})
target_include_directories(engine_scripting PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})