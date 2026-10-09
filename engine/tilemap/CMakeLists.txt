file(GLOB TILEMAP_SOURCES CONFIGURE_DEPENDS *.cpp)
file(GLOB TILEMAP_HEADERS CONFIGURE_DEPENDS *.hpp)

# Maps, tilesets and their script API: no renderer.
add_library(engine_tilemap OBJECT ${TILEMAP_SOURCES} ${TILEMAP_HEADERS})
target_include_directories(engine_tilemap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_tilemap PUBLIC
    engine_events
    engine_ecs
    engine_app
    glm
)

# Drawing them (left out of the server build).
add_library(engine_tilemap_render OBJECT render/TileMapRenderModule.cpp render/TileMapRenderModule.hpp)
target_link_libraries(engine_tilemap_render PUBLIC engine_tilemap engine_renderer_2d)

if(JM_BUILD_TESTS)
  add_subdirectory(tests)
endif()
