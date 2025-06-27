file(GLOB_RECURSE ASYNC_SOURCES
    *.cpp
)

file(GLOB_RECURSE ASYNC_HEADERS
    *.hpp
)

add_library(engine_async ${ASYNC_SOURCES} ${ASYNC_HEADERS})
target_include_directories(engine_async PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})