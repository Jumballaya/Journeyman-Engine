add_executable(test_engine_core
    async/LockFreeQueueTest.cpp
)

target_link_libraries(test_engine_core
    PRIVATE
        engine_async
        GTest::gtest_main
)

gtest_discover_tests(test_engine_core)
