add_executable(test_engine_renderer2d
    PostEffectChainTest.cpp
)

target_link_libraries(test_engine_renderer2d
    PRIVATE
        engine_renderer_2d
        GTest::gtest_main
)

gtest_discover_tests(test_engine_renderer2d
    PROPERTIES TIMEOUT 15
)
