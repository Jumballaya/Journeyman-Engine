add_executable(test_engine_core
    async/LockFreeQueueTest.cpp
    async/ThreadPoolTest.cpp
    tasks/TaskGraphTest.cpp
    tasks/JobSystemTest.cpp
    ecs/WorldTest.cpp
    ecs/ViewTest.cpp
    ecs/component/ComponentRegistryTest.cpp
    ecs/component/ComponentManagerTest.cpp
    ecs/entity/EntityBuilderTest.cpp
    ecs/entity/EntityRefTest.cpp
    ecs/system/SystemSchedulerTest.cpp
    events/EventBusTest.cpp
)

target_link_libraries(test_engine_core
    PRIVATE
        engine_async
        engine_tasks
        engine_ecs
        engine_events
        GTest::gtest_main
)

gtest_discover_tests(test_engine_core
    PROPERTIES TIMEOUT 15
)
