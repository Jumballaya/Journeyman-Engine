#pragma once

// Tag types used by ModuleTraits to express feature-module dependencies.
// Modules that hold a runtime resource another module needs put its tag in
// their Provides list; modules that need the resource put the tag in their
// DependsOn list.
//
// Add new tags here as feature modules introduce new inter-module contracts.
// Keep them small and semantically named — a tag is a capability, not a class.

struct WindowTag {};          // A platform window exists (GLFW, etc.)
struct OpenGLContextTag {};   // An OpenGL context is current on the main thread.
