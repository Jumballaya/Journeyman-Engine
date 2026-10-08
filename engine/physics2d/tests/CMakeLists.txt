# Children following parents: the hierarchy alone, on a bare World.
add_executable(test_engine_physics2d
    TransformHierarchyTest.cpp
    ../TransformHierarchy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/tests/TestEnvironment.cpp
)
target_include_directories(test_engine_physics2d PRIVATE ..)
target_link_libraries(test_engine_physics2d PRIVATE
    engine_logger
    engine_async
    engine_ecs
    glm
    GTest::gtest_main
)
gtest_discover_tests(test_engine_physics2d
    PROPERTIES TIMEOUT 15
)
