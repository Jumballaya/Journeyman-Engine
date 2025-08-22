file(GLOB_RECURSE EVENTS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB_RECURSE EVENTS_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

add_library(engine_events STATIC ${EVENTS_SOURCES} ${EVENTS_HEADERS})
target_include_directories(engine_events PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_events PUBLIC
    engine_logger
    engine_async
    nlohmann_json::nlohmann_json
)