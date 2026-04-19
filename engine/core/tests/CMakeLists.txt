add_executable(test_engine_core
    TestEnvironment.cpp
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
    assets/FileSystemTest.cpp
    assets/AssetManagerTest.cpp
    app/ModuleRegistryTest.cpp
    app/SceneLoaderTest.cpp
)

target_link_libraries(test_engine_core
    PRIVATE
        engine_logger
        engine_async
        engine_tasks
        engine_ecs
        engine_events
        engine_assets
        engine_app
        GTest::gtest_main
)

gtest_discover_tests(test_engine_core
    PROPERTIES TIMEOUT 15
)
