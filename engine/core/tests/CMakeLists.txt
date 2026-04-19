add_executable(test_engine_core
    async/LockFreeQueueTest.cpp
    async/ThreadPoolTest.cpp
    tasks/TaskGraphTest.cpp
    tasks/JobSystemTest.cpp
    ecs/WorldTest.cpp
    ecs/ViewTest.cpp
    ecs/component/ComponentRegistryTest.cpp
    ecs/entity/EntityBuilderTest.cpp
    ecs/entity/EntityRefTest.cpp
)

target_link_libraries(test_engine_core
    PRIVATE
        engine_async
        engine_tasks
        engine_ecs
        GTest::gtest_main
)

gtest_discover_tests(test_engine_core
    PROPERTIES TIMEOUT 15
)
