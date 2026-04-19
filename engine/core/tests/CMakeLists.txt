add_executable(test_engine_core
    async/LockFreeQueueTest.cpp
    async/ThreadPoolTest.cpp
    tasks/TaskGraphTest.cpp
)

target_link_libraries(test_engine_core
    PRIVATE
        engine_async
        engine_tasks
        GTest::gtest_main
)

gtest_discover_tests(test_engine_core
    PROPERTIES TIMEOUT 15
)
