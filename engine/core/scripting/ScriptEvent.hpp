#pragma once

#include <array>

// What physics tells an entity's script; each calls the export of that name
// with the other entity (none for ground events).
enum class ScriptEvent { Overlap, OverlapStart, OverlapEnd, Landed, LeftGround };

inline constexpr std::array<const char*, 5> kScriptEventExports = {"onOverlap", "onOverlapStart", "onOverlapEnd",
                                                                   "onLanded", "onLeftGround"};
