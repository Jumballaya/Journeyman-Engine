# The physics systems and the transform hierarchy, on a bare World.
add_executable(test_engine_physics2d
    TransformHierarchyTest.cpp
    SystemsTest.cpp
    BlockingTest.cpp
    ../TransformHierarchy.cpp
    ../Systems.cpp
    ../Blocking.cpp
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
    DISCOVERY_TIMEOUT 60  # the default 5 s: too short for a cold exe on Windows CI
    PROPERTIES TIMEOUT 15
)
