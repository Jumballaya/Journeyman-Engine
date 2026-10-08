# LockFreeQueue: header-only (the event bus's queue and the audio thread's commands).
add_library(engine_async INTERFACE)
target_include_directories(engine_async INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
