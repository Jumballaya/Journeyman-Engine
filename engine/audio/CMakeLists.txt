file(GLOB_RECURSE AUDIO_SOURCES CONFIGURE_DEPENDS
    *.cpp
)

file(GLOB_RECURSE AUDIO_HEADERS CONFIGURE_DEPENDS
    *.hpp
)

list(FILTER AUDIO_SOURCES EXCLUDE REGEX "/tests/")
list(FILTER AUDIO_HEADERS EXCLUDE REGEX "/tests/")

add_library(engine_audio OBJECT ${AUDIO_SOURCES} ${AUDIO_HEADERS})
target_include_directories(engine_audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(engine_audio SYSTEM PUBLIC ${miniaudio_SOURCE_DIR})
target_link_libraries(engine_audio PUBLIC
    m3
    engine_async
    engine_async
    engine_assets
    engine_ecs
    engine_scripting
    nlohmann_json::nlohmann_json
    miniaudio
    stb_lib
)
if(JM_BUILD_TESTS)
  add_subdirectory(tests)
endif()
