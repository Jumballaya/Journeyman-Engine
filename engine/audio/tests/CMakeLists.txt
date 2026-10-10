# The mixer and front end are pure logic; compile just their sources so the test doesn't pull
# in AudioModule (and with it the whole engine).
add_executable(test_engine_audio
    VoiceManagerTest.cpp
    AudioManagerTest.cpp
    ScriptLoopsTest.cpp
    ../AudioManager.cpp
    ../Voice.cpp
    ../VoiceManager.cpp
    ../SoundBuffer.cpp
    ../miniaudio_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/tests/TestEnvironment.cpp
)
target_include_directories(test_engine_audio PRIVATE ..)
target_include_directories(test_engine_audio SYSTEM PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(test_engine_audio PRIVATE
    engine_logger
    engine_assets
    miniaudio
    stb_lib
    GTest::gtest_main
)
gtest_discover_tests(test_engine_audio
    DISCOVERY_TIMEOUT 60  # the default 5 s: too short for a cold exe on Windows CI
    PROPERTIES TIMEOUT 15
)
