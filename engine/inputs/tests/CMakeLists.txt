add_executable(test_engine_inputs
    InputActionsTest.cpp
    GamepadAndReplayTest.cpp
    RemoteInputTest.cpp
    ../Replay.cpp
    ../RemoteInput.cpp
    ../InputActions.cpp
    ../InputsManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/tests/TestEnvironment.cpp
)
target_include_directories(test_engine_inputs PRIVATE ..)
target_link_libraries(test_engine_inputs PRIVATE
    engine_logger
    engine_events
    nlohmann_json::nlohmann_json
    glm
    GTest::gtest_main
)
gtest_discover_tests(test_engine_inputs
    DISCOVERY_TIMEOUT 60  # the default 5 s: too short for a cold exe on Windows CI
    PROPERTIES TIMEOUT 15
)
