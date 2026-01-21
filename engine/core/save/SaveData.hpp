#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

/**
 * Metadata about a save file.
 */
struct SaveMetadata {
    std::string saveName;          // User-friendly name (e.g., "Save Slot 1")
    std::string timestamp;         // ISO 8601 timestamp
    std::string gameVersion;       // Game version when saved
    std::string screenshotPath;    // Optional thumbnail image path
    uint32_t playTimeSeconds{0};   // Total play time
    std::string currentScene;      // Scene that was active when saved
    
    /**
     * Serializes metadata to JSON.
     */
    nlohmann::json toJson() const {
        nlohmann::json json;
        json["saveName"] = saveName;
        json["timestamp"] = timestamp;
        json["gameVersion"] = gameVersion;
        if (!screenshotPath.empty()) {
            json["screenshotPath"] = screenshotPath;
        }
        json["playTimeSeconds"] = playTimeSeconds;
        json["currentScene"] = currentScene;
        return json;
    }
    
    /**
     * Deserializes metadata from JSON.
     */
    void fromJson(const nlohmann::json& json) {
        if (json.contains("saveName")) {
            saveName = json["saveName"].get<std::string>();
        }
        if (json.contains("timestamp")) {
            timestamp = json["timestamp"].get<std::string>();
        }
        if (json.contains("gameVersion")) {
            gameVersion = json["gameVersion"].get<std::string>();
        }
        if (json.contains("screenshotPath")) {
            screenshotPath = json["screenshotPath"].get<std::string>();
        }
        if (json.contains("playTimeSeconds")) {
            playTimeSeconds = json["playTimeSeconds"].get<uint32_t>();
        }
        if (json.contains("currentScene")) {
            currentScene = json["currentScene"].get<std::string>();
        }
    }
};

/**
 * Complete save game data.
 */
struct SaveData {
    SaveMetadata metadata;
    nlohmann::json sceneState;       // Current scene serialized
    nlohmann::json persistentState;  // Cross-scene data (inventory, unlocks, etc.)
    nlohmann::json gameSettings;     // Player preferences
    
    /**
     * Serializes save data to JSON.
     */
    nlohmann::json toJson() const {
        nlohmann::json json;
        json["metadata"] = metadata.toJson();
        json["sceneState"] = sceneState;
        json["persistentState"] = persistentState;
        json["gameSettings"] = gameSettings;
        return json;
    }
    
    /**
     * Deserializes save data from JSON.
     */
    void fromJson(const nlohmann::json& json) {
        if (json.contains("metadata")) {
            metadata.fromJson(json["metadata"]);
        }
        if (json.contains("sceneState")) {
            sceneState = json["sceneState"];
        }
        if (json.contains("persistentState")) {
            persistentState = json["persistentState"];
        }
        if (json.contains("gameSettings")) {
            gameSettings = json["gameSettings"];
        }
    }
};
