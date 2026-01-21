file(GLOB_RECURSE SYSTEM_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB_RECURSE SYSTEM_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

add_library(engine_systems OBJECT ${SYSTEM_SOURCES} ${SYSTEM_HEADERS})
target_include_directories(engine_systems PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_systems PUBLIC 
    engine_ecs
    engine_components
)
