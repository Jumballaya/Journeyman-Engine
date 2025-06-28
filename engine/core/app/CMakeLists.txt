file(GLOB_RECURSE APP_SOURCES
    *.cpp
)

file(GLOB_RECURSE APP_HEADERS
    *.hpp
)

add_library(engine_app STATIC ${APP_SOURCES} ${APP_HEADERS})
target_include_directories(engine_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_app PUBLIC engine_tasks engine_async engine_assets)