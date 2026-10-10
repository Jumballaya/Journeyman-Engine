add_executable(test_engine_renderer2d
    PostEffectChainTest.cpp
    AtlasManagerTest.cpp
    AtlasManagerDynamicTest.cpp
    SpriteShadowTest.cpp
    SpriteAnimationSystemTest.cpp
    LetterboxTest.cpp
    NullRendererTest.cpp
    PhysicsOverlayTest.cpp
    ParticlesTest.cpp
    LightsTest.cpp
    NormalMapsTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/tests/TestEnvironment.cpp
)

target_link_libraries(test_engine_renderer2d
    PRIVATE
        engine_renderer_2d
        GTest::gtest_main
)

gtest_discover_tests(test_engine_renderer2d
    DISCOVERY_TIMEOUT 60  # the default 5 s: too short for a cold exe on Windows CI
    PROPERTIES TIMEOUT 15
)
