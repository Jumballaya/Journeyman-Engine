# F8's moment on screen: picking and drawing what's picked while the agent
# tools hold the run. Agent build only: journeyman_engine links it, the player doesn't.
add_library(engine_moments OBJECT MomentsModule.cpp MomentsModule.hpp)
target_link_libraries(engine_moments PUBLIC engine_app engine_renderer_2d engine_ui engine_physics_2d glfw)
