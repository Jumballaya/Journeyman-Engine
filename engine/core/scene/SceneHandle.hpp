#pragma once

#include <cstdint>
#include <functional>

/**
 * SceneHandle is a generational handle for referencing scenes.
 * Similar to EntityId, it uses an id and generation to prevent
 * use-after-destroy bugs.
 */
struct SceneHandle {
    uint32_t id{0};
    uint32_t generation{0};
    
    /**
     * Checks if this handle is valid (non-zero ID).
     */
    bool isValid() const {
        return id != 0;
    }
    
    /**
     * Creates an invalid SceneHandle (used for "no scene").
     */
    static SceneHandle invalid() {
        return SceneHandle{0, 0};
    }
    
    // Equality operators
    bool operator==(const SceneHandle& other) const = default;
    bool operator!=(const SceneHandle& other) const {
        return !(*this == other);
    }
};

namespace std {
    template<>
    struct hash<SceneHandle> {
        std::size_t operator()(const SceneHandle& handle) const noexcept {
            // Combine id and generation into a single hash
            return std::hash<uint64_t>{}(static_cast<uint64_t>(handle.generation) << 32 | handle.id);
        }
    };
}
