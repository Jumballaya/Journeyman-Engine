add_subdirectory(core)
add_subdirectory(audio)
add_subdirectory(glfw_window)
add_subdirectory(renderer2d)
add_subdirectory(ui)
add_subdirectory(inputs)
add_subdirectory(physics2d)
add_subdirectory(tilemap)

add_executable(journeyman_engine main.cpp)

target_link_libraries(journeyman_engine
    PRIVATE
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
        engine_renderer_2d
        engine_ui
        engine_physics_2d
        engine_tilemap
)

target_compile_definitions(journeyman_engine PRIVATE
  JM_VERSION="${JM_VERSION}"
  $<$<CONFIG:Debug>:JM_LOG_LEVEL=0>   # TRACE level
  $<$<CONFIG:Release>:JM_LOG_LEVEL=2> # INFO level
)


# The script runtime's host imports (env.ts) match what this engine binds.
if(JM_BUILD_TESTS)
  find_program(JM_NODE node)
  if(JM_NODE)
    add_test(NAME HostApi.EnvTsMatchesTheEngine
             COMMAND ${JM_NODE} ${PROJECT_SOURCE_DIR}/scripts/check-host-api.mjs $<TARGET_FILE:journeyman_engine>)
  else()
    message(STATUS "node not found: skipping the host API check")
  endif()
endif()
