file(GLOB_RECURSE SCENE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB_RECURSE SCENE_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

add_library(engine_scene OBJECT ${SCENE_SOURCES} ${SCENE_HEADERS})
target_include_directories(engine_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_scene PUBLIC 
    engine_ecs
    engine_components
)
