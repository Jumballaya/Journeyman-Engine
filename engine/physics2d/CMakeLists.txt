file(GLOB PHYSICS2D_SOURCES CONFIGURE_DEPENDS
    *.cpp
)

file(GLOB PHYSICS2D_HEADERS CONFIGURE_DEPENDS
    *.hpp
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
# The driver's physics queries (agent/). The agent build links it
# (journeyman_engine, the editor, the server); the player doesn't.
add_library(engine_physics_2d_agent OBJECT agent/Physics2DAgentModule.cpp)
target_link_libraries(engine_physics_2d_agent PUBLIC engine_physics_2d)
if(JM_BUILD_TESTS)
  add_subdirectory(tests)
endif()
