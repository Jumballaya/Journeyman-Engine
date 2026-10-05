file(GLOB EDITOR_SOURCES CONFIGURE_DEPENDS *.cpp panels/*.cpp)

# Fonts compiled in: the editor ships as one executable.
set(EDITOR_FONTS
    geist_regular fonts/Geist-Regular.ttf
    geist_medium fonts/Geist-Medium.ttf
    geist_semibold fonts/Geist-SemiBold.ttf
    geist_mono fonts/GeistMono-Regular.ttf
    phosphor fonts/Phosphor.ttf
    phosphor_fill fonts/Phosphor-Fill.ttf
)
set(EMBEDDED_FONT_SOURCES)
list(LENGTH EDITOR_FONTS count)
math(EXPR last "${count} - 1")
foreach(i RANGE 0 ${last} 2)
  math(EXPR j "${i} + 1")
  list(GET EDITOR_FONTS ${i} name)
  list(GET EDITOR_FONTS ${j} file)
  set(out ${CMAKE_CURRENT_BINARY_DIR}/embedded/${name}.cpp)
  add_custom_command(
      OUTPUT ${out}
      COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/${file} -DOUTPUT=${out} -DNAME=${name}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${file} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  )
  list(APPEND EMBEDDED_FONT_SOURCES ${out})
endforeach()

add_executable(journeyman_editor ${EDITOR_SOURCES} ${EMBEDDED_FONT_SOURCES})
target_include_directories(journeyman_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(journeyman_editor PRIVATE
    engine_logger
    engine_async
    engine_tasks
    engine_events
    engine_ecs
    engine_assets
    engine_app
    engine_scripting
    engine_audio
    engine_glfw_window
    engine_inputs
    engine_renderer_2d
    engine_ui
    engine_physics_2d
    engine_tilemap
    imgui_lib
    nfd
)
target_compile_definitions(journeyman_editor PRIVATE
  GLFW_INCLUDE_NONE  # glad provides the GL headers
  $<$<CONFIG:Debug>:JM_LOG_LEVEL=0>
  $<$<CONFIG:Release>:JM_LOG_LEVEL=2>
)
