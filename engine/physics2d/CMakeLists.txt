file(GLOB_RECURSE PHYSICS2D_SOURCES CONFIGURE_DEPENDS
    *.cpp
    gl/*.cpp
)

file(GLOB_RECURSE PHYSICS2D_HEADERS CONFIGURE_DEPENDS
    *.hpp
    gl/*.hpp
)

add_library(engine_physics_2d OBJECT ${PHYSICS2D_SOURCES} ${PHYSICS2D_HEADERS})
target_include_directories(engine_physics_2d PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(engine_physics_2d PUBLIC
    engine_events
    engine_ecs
    engine_app
    glm
)