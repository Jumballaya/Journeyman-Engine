file(GLOB_RECURSE ASSETS_SOURCES CONFIGURE_DEPENDS
    *.cpp
)

file(GLOB_RECURSE ASSETS_HEADERS CONFIGURE_DEPENDS
    *.hpp
)

add_library(engine_assets STATIC ${ASSETS_SOURCES} ${ASSETS_HEADERS})
target_include_directories(engine_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_assets PUBLIC engine_logger engine_tasks engine_async)