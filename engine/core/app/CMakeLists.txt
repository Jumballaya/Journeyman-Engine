file(GLOB_RECURSE APP_SOURCES
    *.cpp
)

file(GLOB_RECURSE APP_HEADERS
    *.hpp
)

add_library(engine_app ${APP_SOURCES} ${APP_HEADERS})
target_include_directories(engine_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})