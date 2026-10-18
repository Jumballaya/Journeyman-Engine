file(GLOB_RECURSE RENDERER2D_SOURCES CONFIGURE_DEPENDS
    *.cpp
    gl/*.cpp
)

file(GLOB_RECURSE RENDERER2D_HEADERS CONFIGURE_DEPENDS
    *.hpp
    gl/*.hpp
)

list(FILTER RENDERER2D_SOURCES EXCLUDE REGEX "/(tests|agent)/")
list(FILTER RENDERER2D_HEADERS EXCLUDE REGEX "/(tests|agent)/")

add_library(engine_renderer_2d OBJECT ${RENDERER2D_SOURCES} ${RENDERER2D_HEADERS})
target_include_directories(engine_renderer_2d PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(engine_renderer_2d SYSTEM PUBLIC ${stb_SOURCE_DIR})
target_link_libraries(engine_renderer_2d PUBLIC
    engine_events
    engine_ecs
    engine_app
    glm
    glad_lib
    glfw
    stb_lib
)

# The agent tools' view of the frame (agent/): captures, the physics overlay,
# the driver's mouse. The agent build links it (journeyman_engine, the
# editor); the player doesn't.
add_library(engine_renderer_2d_agent OBJECT agent/Renderer2DAgentModule.cpp agent/PhysicsOverlay.cpp agent/PhysicsOverlay.hpp)
target_link_libraries(engine_renderer_2d_agent PUBLIC engine_renderer_2d)

if(JM_BUILD_TESTS)
  add_subdirectory(tests)
endif()