add_executable(test_engine_ui
    FontRegistryTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/tests/TestEnvironment.cpp
)
target_link_libraries(test_engine_ui PRIVATE
    engine_ui
    GTest::gtest_main
)
gtest_discover_tests(test_engine_ui
    PROPERTIES TIMEOUT 15
)
