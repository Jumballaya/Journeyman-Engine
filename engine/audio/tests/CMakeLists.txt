# The mixer and front end are pure logic; compile just their sources so the test doesn't pull
# in AudioModule (and with it the whole engine).
add_executable(test_engine_audio
    VoiceManagerTest.cpp
    AudioManagerTest.cpp
    ../AudioManager.cpp
    ../Voice.cpp
    ../VoiceManager.cpp
    ../SoundBuffer.cpp
    ../miniaudio_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/tests/TestEnvironment.cpp
)
target_include_directories(test_engine_audio PRIVATE .. ${miniaudio_SOURCE_DIR})
target_link_libraries(test_engine_audio PRIVATE
    engine_logger
    engine_assets
    miniaudio
    stb_lib
    GTest::gtest_main
)
gtest_discover_tests(test_engine_audio
    PROPERTIES TIMEOUT 15
)
