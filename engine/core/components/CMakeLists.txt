file(GLOB_RECURSE COMPONENT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB_RECURSE COMPONENT_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

add_library(engine_components OBJECT ${COMPONENT_SOURCES} ${COMPONENT_HEADERS})
target_include_directories(engine_components PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_components PUBLIC 
    engine_ecs
)
