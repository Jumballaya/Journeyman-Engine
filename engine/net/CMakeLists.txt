file(GLOB NET_SOURCES CONFIGURE_DEPENDS *.cpp)
file(GLOB NET_HEADERS CONFIGURE_DEPENDS *.hpp)

# Multiplayer: sessions over ENet, shared entities, the Net script API.
add_library(engine_net OBJECT ${NET_SOURCES} ${NET_HEADERS})
target_include_directories(engine_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_net PUBLIC
    engine_events
    engine_ecs
    engine_app
    engine_scripting
    engine_inputs
    nlohmann_json::nlohmann_json
    enet_lib
)
