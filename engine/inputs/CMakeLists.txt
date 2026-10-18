file(GLOB INPUTS_SOURCES CONFIGURE_DEPENDS *.cpp)
file(GLOB INPUTS_HEADERS CONFIGURE_DEPENDS *.hpp)

# Keys, actions, replays and the script API: no window or device library.
add_library(engine_inputs OBJECT ${INPUTS_SOURCES} ${INPUTS_HEADERS})
target_include_directories(engine_inputs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(engine_inputs PUBLIC
    engine_events
    engine_app
    nlohmann_json::nlohmann_json
    glm
)

# The machine's keyboard and gamepads (Devices.hpp): GLFW for the game and the
# editor, none for the server build. Link exactly one.
add_library(engine_inputs_glfw OBJECT devices/GlfwDevices.cpp)
target_link_libraries(engine_inputs_glfw PUBLIC engine_inputs glfw)
add_library(engine_inputs_nodevices OBJECT devices/NoDevices.cpp)
target_link_libraries(engine_inputs_nodevices PUBLIC engine_inputs)

# The agent tools' keys (agent/): replay files, the driver's down|up|press, its
# recording. The agent build links it (journeyman_engine, the editor, the
# server); the player doesn't.
add_library(engine_inputs_agent OBJECT agent/InputsAgentModule.cpp agent/Replay.cpp agent/Replay.hpp)
target_link_libraries(engine_inputs_agent PUBLIC engine_inputs)

if(JM_BUILD_TESTS)
  add_subdirectory(tests)
endif()
