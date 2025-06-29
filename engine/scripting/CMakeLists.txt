file(GLOB_RECURSE SCRIPTING_SOURCES CONFIGURE_DEPENDS
    *.cpp
)

file(GLOB_RECURSE SCRIPTING_HEADERS CONFIGURE_DEPENDS
    *.hpp
)

add_library(engine_scripting OBJECT ${SCRIPTING_SOURCES} ${SCRIPTING_HEADERS})
target_include_directories(engine_scripting PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${wasm3_SOURCE_DIR}/source
)
target_link_libraries(engine_scripting PUBLIC
    m3
    engine_tasks
    engine_async
    engine_assets
    engine_ecs
    nlohmann_json::nlohmann_json
)