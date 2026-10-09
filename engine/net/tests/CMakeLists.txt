add_executable(test_engine_net
    WireTest.cpp
    TransportTest.cpp
    ../EnetTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/tests/TestEnvironment.cpp
)
target_include_directories(test_engine_net PRIVATE ..)
target_link_libraries(test_engine_net PRIVATE
    engine_logger
    enet_lib
    GTest::gtest_main
)
gtest_discover_tests(test_engine_net
    DISCOVERY_TIMEOUT 60  # the default 5 s: too short for a cold exe on Windows CI
    PROPERTIES TIMEOUT 20
)
