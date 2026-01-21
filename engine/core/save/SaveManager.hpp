#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "../scene/SceneManager.hpp"
#include "../ecs/World.hpp"
#include "../assets/AssetManager.hpp"
#include "SaveData.hpp"

/**
 * SaveManager handles saving and loading game state.
 * Supports multiple save slots, persistent state, and auto-save.
 */
class SaveManager {
public:
    /**
     * Creates a SaveManager.
     * @param scenes - Reference to SceneManager
     * @param world - Reference to ECS World
     * @param assets - Reference to AssetManager
     */
    SaveManager(SceneManager& scenes, World& world, AssetManager& assets);
    
    // ====================================================================
    // Save Operations
    // ====================================================================
    
    /**
     * Saves the game to a named slot.
     * @param slotName - Name of the save slot
     * @returns true on success, false on failure
     */
    bool save(const std::string& slotName);
    
    /**
     * Saves the game to a specific file path.
     * @param path - Path to save file
     * @returns true on success, false on failure
     */
    bool saveToFile(const std::filesystem::path& path);
    
    /**
     * Performs a quick save (to a special quick-save slot).
     * @returns true on success
     */
    bool quickSave();
    
    /**
     * Performs an auto-save (if enabled).
     * @returns true on success
     */
    bool autoSave();
    
    // ====================================================================
    // Load Operations
    // ====================================================================
    
    /**
     * Loads a game from a named slot.
     * @param slotName - Name of the save slot
     * @returns true on success, false on failure
     */
    bool load(const std::string& slotName);
    
    /**
     * Loads a game from a specific file path.
     * @param path - Path to save file
     * @returns true on success, false on failure
     */
    bool loadFromFile(const std::filesystem::path& path);
    
    /**
     * Loads the quick-save slot.
     * @returns true on success
     */
    bool quickLoad();
    
    // ====================================================================
    // Save Slot Management
    // ====================================================================
    
    /**
     * Lists all available save slots.
     * @returns Vector of save metadata
     */
    std::vector<SaveMetadata> listSaves();
    
    /**
     * Deletes a save slot.
     * @param slotName - Name of the save slot to delete
     * @returns true on success
     */
    bool deleteSave(const std::string& slotName);
    
    /**
     * Renames a save slot.
     * @param oldName - Current name
     * @param newName - New name
     * @returns true on success
     */
    bool renameSave(const std::string& oldName, const std::string& newName);
    
    /**
     * Copies a save slot.
     * @param source - Source slot name
     * @param dest - Destination slot name
     * @returns true on success
     */
    bool copySave(const std::string& source, const std::string& dest);
    
    // ====================================================================
    // Persistent State
    // ====================================================================
    
    /**
     * Sets a persistent state value (survives scene transitions).
     * @param key - Key name
     * @param value - JSON value
     */
    void setPersistent(const std::string& key, const nlohmann::json& value);
    
    /**
     * Gets a persistent state value.
     * @param key - Key name
     * @returns JSON value, or null if not found
     */
    nlohmann::json getPersistent(const std::string& key) const;
    
    /**
     * Checks if a persistent state key exists.
     * @param key - Key name
     * @returns true if key exists
     */
    bool hasPersistent(const std::string& key) const;
    
    /**
     * Clears a persistent state key.
     * @param key - Key name
     */
    void clearPersistent(const std::string& key);
    
    /**
     * Clears all persistent state.
     */
    void clearAllPersistent();
    
    // ====================================================================
    // Configuration
    // ====================================================================
    
    /**
     * Sets the save directory.
     * @param dir - Directory path for save files
     */
    void setSaveDirectory(const std::filesystem::path& dir);
    
    /**
     * Gets the save directory.
     */
    const std::filesystem::path& getSaveDirectory() const {
        return _saveDirectory;
    }
    
    /**
     * Sets the auto-save interval.
     * @param seconds - Interval in seconds
     */
    void setAutoSaveInterval(float seconds);
    
    /**
     * Gets the auto-save interval.
     */
    float getAutoSaveInterval() const {
        return _autoSaveInterval;
    }
    
    /**
     * Enables or disables auto-save.
     * @param enable - True to enable
     */
    void enableAutoSave(bool enable);
    
    /**
     * Checks if auto-save is enabled.
     */
    bool isAutoSaveEnabled() const {
        return _autoSaveEnabled;
    }
    
    /**
     * Updates the save manager (call every frame).
     * Handles auto-save timer.
     * @param dt - Delta time in seconds
     */
    void update(float dt);
    
private:
    SceneManager& _scenes;
    World& _world;
    AssetManager& _assets;
    
    std::filesystem::path _saveDirectory{"saves"};
    nlohmann::json _persistentState;
    nlohmann::json _gameSettings;
    
    float _autoSaveInterval{300.0f};  // 5 minutes default
    float _autoSaveTimer{0.0f};
    bool _autoSaveEnabled{false};
    
    /**
     * Builds save data from current game state.
     */
    SaveData buildSaveData();
    
    /**
     * Applies save data to restore game state.
     */
    void applySaveData(const SaveData& data);
    
    /**
     * Gets the file path for a save slot.
     * @param slotName - Save slot name
     * @returns Full path to save file
     */
    std::filesystem::path getSavePath(const std::string& slotName) const;
    
    /**
     * Generates a timestamp string.
     */
    std::string generateTimestamp() const;
};
