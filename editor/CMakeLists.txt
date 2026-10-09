file(GLOB EDITOR_SOURCES CONFIGURE_DEPENDS *.cpp panels/*.cpp editors/*.cpp)

# Fonts and the icon compiled in: the editor ships as one executable.
set(EDITOR_FONTS
    editor_icon icon.png
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

# macOS: AppKit tells trackpad from wheel and sees pinches (Scroll.mm); the
# user's light or dark appearance (SystemAppearance.mm).
# Compiled as Objective-C++ by the C++ compiler (enabling OBJCXX changes how GLFW's .m files build).
# AppKit comes in with GLFW.
if(APPLE)
  list(APPEND EDITOR_SOURCES Scroll.mm SystemAppearance.mm)
  set_source_files_properties(Scroll.mm SystemAppearance.mm PROPERTIES LANGUAGE CXX COMPILE_OPTIONS "-xobjective-c++")
endif()

add_executable(journeyman_editor ${EDITOR_SOURCES} ${EMBEDDED_FONT_SOURCES})
target_include_directories(journeyman_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(journeyman_editor PRIVATE
    engine_logger
    engine_async
    engine_async
    engine_events
    engine_ecs
    engine_assets
    engine_app
    engine_scripting
    engine_audio
    engine_glfw_window
    engine_inputs
    engine_inputs_glfw
    engine_renderer_2d
    engine_ui
    engine_physics_2d
    engine_tilemap
    engine_tilemap_render
    engine_net
    imgui_lib
    nfd
)
target_compile_definitions(journeyman_editor PRIVATE
  GLFW_INCLUDE_NONE  # glad provides the GL headers
  $<$<CONFIG:Debug>:JM_LOG_LEVEL=0>
  $<$<CONFIG:Release>:JM_LOG_LEVEL=2>
)

if(JM_BUILD_TESTS)
  add_subdirectory(tests)
endif()
