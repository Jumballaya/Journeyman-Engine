#pragma once

// Capabilities one module provides and others depend on (ModuleTraits).

struct WindowTag {};          // A platform window exists (GLFW, etc.)
struct OpenGLContextTag {};   // An OpenGL context is current on the main thread.
struct Renderer2DTag {};       // Renderer2DModule is initialized (sprite renderer, textures).
struct InputsTag {};           // InputsModule is registered (keys, actions, per-player input).
