add_subdirectory(core)

add_executable(journeyman_engine main.cpp)

target_link_libraries(journeyman_engine
    PRIVATE
        engine_core
)