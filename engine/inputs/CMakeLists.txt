file(GLOB_RECURSE IMPORTED_CONFIGURATIONS_SOURCES CONFIGURE_DEPENDS
    *.cpp
)

file(GLOB_RECURSE IMPORTED_CONFIGURATIONS_HEADERS CONFIGURE_DEPENDS
    *.hpp
)

add_library(engine_inputs OBJECT ${IMPORTED_CONFIGURATIONS_SOURCES} ${IMPORTED_CONFIGURATIONS_HEADERS})
target_include_directories(engine_inputs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(engine_inputs PUBLIC
    engine_events
    engine_app
    engine_glfw_window
    nlohmann_json::nlohmann_json
    glfw
)