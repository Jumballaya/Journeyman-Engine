add_executable(test_engine_core
    TestEnvironment.cpp
    async/LockFreeQueueTest.cpp
    ecs/WorldTest.cpp
    ecs/HierarchyTest.cpp
    ecs/ViewTest.cpp
    ecs/archetype/ArchetypeTest.cpp
    ecs/archetype/ArchetypeSetTest.cpp
    ecs/component/ComponentRegistryTest.cpp
    ecs/component/ComponentVtableTest.cpp
    ecs/component/SchemaJsonTest.cpp
    ecs/entity/EntityBuilderTest.cpp
    ecs/entity/EntityRefTest.cpp
    ecs/prefab/PrefabTest.cpp
    ecs/system/SystemSchedulerTest.cpp
    events/EventBusTest.cpp
    assets/FileSystemTest.cpp
    assets/AssetManagerTest.cpp
    assets/AssetRegistryTest.cpp
    assets/ArchiveTest.cpp
    scripting/ScriptInstanceTest.cpp
    app/ModuleRegistryTest.cpp
    app/SceneLoaderTest.cpp
    app/SceneManagerTest.cpp
    app/ApplicationTest.cpp
    app/GameStateTest.cpp
    app/ErrorReportTest.cpp
    app/EngineStateTest.cpp
    app/EngineDriverTest.cpp
    app/EntitySpawnerTest.cpp
    app/PlaySessionTest.cpp
    app/ProfilerTest.cpp
    app/PlatformTest.cpp
    app/DevOptionsTest.cpp
)

target_link_libraries(test_engine_core
    PRIVATE
        engine_logger
        engine_async
        engine_async
        engine_ecs
        engine_events
        engine_assets
        engine_app
        engine_dev_options
        GTest::gtest_main
)

gtest_discover_tests(test_engine_core
    DISCOVERY_TIMEOUT 60  # the default 5 s: too short for a cold exe on Windows CI
    PROPERTIES TIMEOUT 15
)

# journeyman_player's DevOptions, on their own: nothing else links them.
add_executable(test_engine_player app/DevOptionsTest.cpp)
target_compile_definitions(test_engine_player PRIVATE JM_PLAYER_BUILD)
target_link_libraries(test_engine_player PRIVATE engine_dev_options_player GTest::gtest_main)
gtest_discover_tests(test_engine_player DISCOVERY_TIMEOUT 60 PROPERTIES TIMEOUT 15)
