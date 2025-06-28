file(GLOB_RECURSE TASKS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB_RECURSE TASKS_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

add_library(engine_tasks STATIC ${TASKS_SOURCES} ${TASKS_HEADERS})
target_include_directories(engine_tasks PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine_tasks PUBLIC engine_async)