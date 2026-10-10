add_subdirectory(core)
add_subdirectory(audio)
add_subdirectory(glfw_window)
add_subdirectory(renderer2d)
add_subdirectory(ui)
add_subdirectory(inputs)
add_subdirectory(physics2d)
add_subdirectory(tilemap)
add_subdirectory(net)

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
        engine_inputs_glfw
        engine_renderer_2d
        engine_ui
        engine_physics_2d
        engine_tilemap
        engine_tilemap_render
        engine_net
)

target_compile_definitions(journeyman_engine PRIVATE
  JM_VERSION="${JM_VERSION}"
  $<$<CONFIG:Debug>:JM_LOG_LEVEL=0>   # TRACE level
  $<$<CONFIG:Release>:JM_LOG_LEVEL=2> # INFO level
)

# The dedicated server: the same engine, linked without the frontend modules
# (window, renderer, UI, audio). A module is in a build when it's linked
# (REGISTER_MODULE), so leaving their libraries out is all it takes.
add_executable(journeyman_server server_main.cpp)
target_link_libraries(journeyman_server
    PRIVATE
        engine_logger
        engine_async
        engine_events
        engine_ecs
        engine_assets
        engine_app
        engine_scripting
        engine_inputs
        engine_inputs_nodevices
        engine_physics_2d
        engine_tilemap
        engine_net
)
target_compile_definitions(journeyman_server PRIVATE
  $<$<CONFIG:Debug>:JM_LOG_LEVEL=0>
  $<$<CONFIG:Release>:JM_LOG_LEVEL=2>
)


# The script runtime's host imports (env.ts) and component fields match this engine.
if(JM_BUILD_TESTS)
  find_program(JM_NODE node)
  if(JM_NODE)
    add_test(NAME HostApi.RuntimeMatchesTheEngine
             COMMAND ${JM_NODE} ${PROJECT_SOURCE_DIR}/scripts/check-host-api.mjs $<TARGET_FILE:journeyman_engine>)
  else()
    message(STATUS "node not found: skipping the host API check")
  endif()
endif()
