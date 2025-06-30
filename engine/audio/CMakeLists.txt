file(GLOB_RECURSE AUDIO_SOURCES CONFIGURE_DEPENDS
    *.cpp
)

file(GLOB_RECURSE AUDIO_HEADERS CONFIGURE_DEPENDS
    *.hpp
)

add_library(engine_audio OBJECT ${AUDIO_SOURCES} ${AUDIO_HEADERS})
target_include_directories(engine_audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${miniaudio_SOURCE_DIR}
)
target_link_libraries(engine_audio PUBLIC
    m3
    engine_tasks
    engine_async
    engine_assets
    engine_ecs
    engine_scripting
    nlohmann_json::nlohmann_json
    miniaudio
)