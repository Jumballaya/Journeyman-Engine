add_subdirectory(core)
add_subdirectory(audio)

add_executable(journeyman_engine main.cpp)

target_link_libraries(journeyman_engine
    PRIVATE
        engine_logger
        engine_async
        engine_tasks
        engine_ecs
        engine_assets
        engine_app
        engine_scripting
        engine_audio
)

target_compile_definitions(journeyman_engine PRIVATE
  $<$<CONFIG:Debug>:JM_LOG_LEVEL=0>   # TRACE level
  $<$<CONFIG:Release>:JM_LOG_LEVEL=2> # INFO level
)

