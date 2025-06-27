file(GLOB_RECURSE ECS_SOURCES
    *.cpp
)

file(GLOB_RECURSE ECS_HEADERS
    *.hpp
)

add_library(engine_ecs ${ECS_SOURCES} ${ECS_HEADERS})
target_include_directories(engine_ecs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})