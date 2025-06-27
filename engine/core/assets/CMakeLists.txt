file(GLOB_RECURSE ASSETS_SOURCES
    *.cpp
)

file(GLOB_RECURSE ASSETS_HEADERS
    *.hpp
)

add_library(engine_assets ${ASSETS_SOURCES} ${ASSETS_HEADERS})
target_include_directories(engine_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})