file(GLOB_RECURSE TASKS_SOURCES
    *.cpp
)

file(GLOB_RECURSE TASKS_HEADERS
    *.hpp
)

add_library(engine_tasks ${TASKS_SOURCES} ${TASKS_HEADERS})
target_include_directories(engine_tasks PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})