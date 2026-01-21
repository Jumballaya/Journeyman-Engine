file(GLOB_RECURSE SAVE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB_RECURSE SAVE_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

add_library(engine_save OBJECT ${SAVE_SOURCES} ${SAVE_HEADERS})
target_include_directories(engine_save PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_save PUBLIC 
    engine_ecs
    engine_scene
    engine_components
)
