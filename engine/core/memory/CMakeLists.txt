file(GLOB_RECURSE MEMORY_SOURCES
    *.cpp
)

file(GLOB_RECURSE MEMORY_HEADERS
    *.hpp
)

add_library(engine_memory ${MEMORY_SOURCES} ${MEMORY_HEADERS})
target_include_directories(engine_memory PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})