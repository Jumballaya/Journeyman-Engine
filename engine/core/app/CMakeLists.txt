file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    *.cpp
)

file(GLOB_RECURSE APP_HEADERS CONFIGURE_DEPENDS
    *.hpp
)

list(FILTER APP_SOURCES EXCLUDE REGEX "/dev_options/")

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

# Where DevOptions::fromEnvironment comes from: the JM_* variables (the engine
# jm runs, the server, tests), or none at all (journeyman_player, what
# exported games ship). Link exactly one.
add_library(engine_dev_options OBJECT dev_options/Environment.cpp)
target_link_libraries(engine_dev_options PUBLIC engine_app)
add_library(engine_dev_options_player OBJECT dev_options/Player.cpp)
target_link_libraries(engine_dev_options_player PUBLIC engine_app)
