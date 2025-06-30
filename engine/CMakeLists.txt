if (MSVC)
  # unsure of what to do for MSVC here for the same compiler warnings/errors
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

add_subdirectory(core)

add_executable(journeyman_engine main.cpp)

target_link_libraries(journeyman_engine
    PRIVATE
        engine_async
        engine_tasks
        engine_ecs
        engine_assets
        engine_app
        engine_scripting
)