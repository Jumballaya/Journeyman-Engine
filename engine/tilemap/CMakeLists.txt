file(GLOB TILEMAP_SOURCES CONFIGURE_DEPENDS *.cpp)
file(GLOB TILEMAP_HEADERS CONFIGURE_DEPENDS *.hpp)

add_library(engine_tilemap OBJECT ${TILEMAP_SOURCES} ${TILEMAP_HEADERS})
target_include_directories(engine_tilemap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_tilemap PUBLIC
    engine_events
    engine_ecs
    engine_app
    engine_renderer_2d
    glm
)

if(JM_BUILD_TESTS)
  add_subdirectory(tests)
endif()
