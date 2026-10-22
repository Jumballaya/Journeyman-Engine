# Picking with the engine and its renderer (none: no GPU).
add_executable(test_engine_moments
    MomentsTest.cpp
    InspectDrawTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/tests/TestEnvironment.cpp
)
target_link_libraries(test_engine_moments PRIVATE
    engine_moments engine_ui engine_tilemap engine_tilemap_render engine_renderer_2d engine_renderer_2d_agent engine_physics_2d engine_agent_tools GTest::gtest_main)
gtest_discover_tests(test_engine_moments
    DISCOVERY_TIMEOUT 60
    PROPERTIES TIMEOUT 15
)
