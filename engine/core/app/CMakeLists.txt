file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    *.cpp
)

file(GLOB_RECURSE APP_HEADERS CONFIGURE_DEPENDS
    *.hpp
)

list(FILTER APP_SOURCES EXCLUDE REGEX "/(agent|player)/")

add_library(engine_app STATIC ${APP_SOURCES} ${APP_HEADERS})
target_include_directories(engine_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_app PUBLIC
    engine_logger
    engine_async
    engine_async
    engine_events
    engine_assets
    engine_ecs
    engine_scripting
    glm
    nlohmann_json::nlohmann_json
)

# The agent build's tools (AgentTools.hpp) and DevOptions::fromEnvironment
# from the JM_* variables: the engine jm runs, the editor, the server, tests.
# Or none of either (journeyman_player, what exported games ship). Link
# exactly one.
file(GLOB AGENT_SOURCES CONFIGURE_DEPENDS agent/*.cpp)
add_library(engine_agent_tools OBJECT ${AGENT_SOURCES})
target_include_directories(engine_agent_tools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/agent)
target_link_libraries(engine_agent_tools PUBLIC engine_app)
add_library(engine_agent_tools_none OBJECT player/Player.cpp)
target_link_libraries(engine_agent_tools_none PUBLIC engine_app)
