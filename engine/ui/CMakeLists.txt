file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS *.cpp)
file(GLOB_RECURSE UI_HEADERS CONFIGURE_DEPENDS *.hpp)

list(FILTER UI_SOURCES EXCLUDE REGEX "/tests/")
list(FILTER UI_HEADERS EXCLUDE REGEX "/tests/")

add_library(engine_ui OBJECT ${UI_SOURCES} ${UI_HEADERS})
target_include_directories(engine_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(engine_ui
    PUBLIC
        engine_assets
        engine_app
        engine_logger
        engine_ecs
        engine_scripting
        engine_renderer_2d
    PRIVATE
        stb_lib
)

if(JM_BUILD_TESTS)
  add_subdirectory(tests)
endif()
