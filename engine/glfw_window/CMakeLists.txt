file(GLOB_RECURSE WINDOW_SOURCES CONFIGURE_DEPENDS
    *.cpp
)

file(GLOB_RECURSE WINDOW_HEADERS CONFIGURE_DEPENDS
    *.hpp
)

add_library(engine_glfw_window OBJECT ${WINDOW_SOURCES} ${WINDOW_HEADERS})
target_include_directories(engine_glfw_window PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(engine_glfw_window PUBLIC
    engine_app
    nlohmann_json::nlohmann_json
    glfw
)