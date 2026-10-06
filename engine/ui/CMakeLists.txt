file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS *.cpp)
file(GLOB_RECURSE UI_HEADERS CONFIGURE_DEPENDS *.hpp)

list(FILTER UI_SOURCES EXCLUDE REGEX "/tests/")
list(FILTER UI_HEADERS EXCLUDE REGEX "/tests/")

# Embed the fallback UI font (Press Start 2P, SIL OFL — see fonts/) so text
# renders in projects that don't ship a font of their own.
set(JM_DEFAULT_FONT ${CMAKE_CURRENT_SOURCE_DIR}/fonts/DefaultFont.ttf)
set(JM_DEFAULT_FONT_CPP ${CMAKE_CURRENT_BINARY_DIR}/DefaultFontData.cpp)
add_custom_command(
  OUTPUT ${JM_DEFAULT_FONT_CPP}
  COMMAND ${CMAKE_COMMAND} -DINPUT=${JM_DEFAULT_FONT} -DOUTPUT=${JM_DEFAULT_FONT_CPP}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/fonts/embed.cmake
  DEPENDS ${JM_DEFAULT_FONT} ${CMAKE_CURRENT_SOURCE_DIR}/fonts/embed.cmake
  VERBATIM)

add_library(engine_ui OBJECT ${UI_SOURCES} ${UI_HEADERS} ${JM_DEFAULT_FONT_CPP})
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
