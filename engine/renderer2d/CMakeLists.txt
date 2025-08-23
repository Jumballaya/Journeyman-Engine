file(GLOB_RECURSE RENDERER2D_SOURCES CONFIGURE_DEPENDS
    *.cpp
    gl/*.cpp
)

file(GLOB_RECURSE RENDERER2D_HEADERS CONFIGURE_DEPENDS
    *.hpp
    gl/*.hpp
)

add_library(engine_renderer_2d OBJECT ${RENDERER2D_SOURCES} ${WINDOW_HEADERS})
target_include_directories(engine_renderer_2d PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(engine_renderer_2d PUBLIC
    engine_events
    glm
    glad
    glfw
    stb
)