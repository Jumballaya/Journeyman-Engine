#include "SaveManager.hpp"
#include "../scene/SceneSerializer.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <algorithm>

SaveManager::SaveManager(SceneManager& scenes, World& world, AssetManager& assets)
    : _scenes(scenes), _world(world), _assets(assets) {
    // Create save directory if it doesn't exist
    if (!std::filesystem::exists(_saveDirectory)) {
        std::filesystem::create_directories(_saveDirectory);
    }
}

SaveData SaveManager::buildSaveData() {
    SaveData saveData;
    
    // Build metadata
    saveData.metadata.timestamp = generateTimestamp();
    saveData.metadata.gameVersion = "1.0.0";  // TODO: Get from GameManifest
    saveData.metadata.playTimeSeconds = 0;    // TODO: Track play time
    
    // Get current scene
    Scene* activeScene = _scenes.getActiveScene();
    if (activeScene) {
        saveData.metadata.currentScene = activeScene->getName();
    }
    
    // Serialize current scene
    if (activeScene) {
        SceneSerializer serializer(_world, _assets);
        saveData.sceneState = serializer.serializeScene(*activeScene);
    }
    
    // Copy persistent state
    saveData.persistentState = _persistentState;
    
    // Copy game settings
    saveData.gameSettings = _gameSettings;
    
    return saveData;
}

void SaveManager::applySaveData(const SaveData& data) {
    // Restore persistent state
    _persistentState = data.persistentState;
    
    // Restore game settings
    _gameSettings = data.gameSettings;
    
    // Load scene
    if (!data.metadata.currentScene.empty()) {
        // Unload current scenes
        _scenes.unloadAllScenes();
        
        // Load saved scene
        SceneHandle handle = _scenes.loadScene(data.metadata.currentScene);
        Scene* scene = _scenes.getScene(handle);
        
        if (scene && !data.sceneState.empty()) {
            // Deserialize scene state
            SceneSerializer serializer(_world, _assets);
            // Use the handle we already have
            serializer.deserializeScene(*scene, data.sceneState, handle);
        }
    }
}

bool SaveManager::save(const std::string& slotName) {
    if (slotName.empty()) {
        return false;
    }
    
    SaveData saveData = buildSaveData();
    saveData.metadata.saveName = slotName;
    
    std::filesystem::path savePath = getSavePath(slotName);
    return saveToFile(savePath);
}

bool SaveManager::saveToFile(const std::filesystem::path& path) {
    SaveData saveData = buildSaveData();
    if (saveData.metadata.saveName.empty()) {
        saveData.metadata.saveName = path.stem().string();
    }
    
    try {
        // Ensure directory exists
        std::filesystem::create_directories(path.parent_path());
        
        // Serialize to JSON
        nlohmann::json json = saveData.toJson();
        
        // Write to file
        std::ofstream file(path);
        if (!file.is_open()) {
            return false;
        }
        
        file << json.dump(2);  // Pretty print with 2-space indent
        file.close();
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool SaveManager::quickSave() {
    return save("quicksave");
}

bool SaveManager::autoSave() {
    return save("autosave");
}

bool SaveManager::load(const std::string& slotName) {
    if (slotName.empty()) {
        return false;
    }
    
    std::filesystem::path savePath = getSavePath(slotName);
    return loadFromFile(savePath);
}

bool SaveManager::loadFromFile(const std::filesystem::path& path) {
    try {
        if (!std::filesystem::exists(path)) {
            return false;  // File doesn't exist
        }
        
        // Read file
        std::ifstream file(path);
        if (!file.is_open()) {
            return false;
        }
        
        nlohmann::json json;
        file >> json;
        file.close();
        
        // Deserialize
        SaveData saveData;
        saveData.fromJson(json);
        
        // Apply save data
        applySaveData(saveData);
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool SaveManager::quickLoad() {
    return load("quicksave");
}

std::vector<SaveMetadata> SaveManager::listSaves() {
    std::vector<SaveMetadata> saves;
    
    if (!std::filesystem::exists(_saveDirectory)) {
        return saves;
    }
    
    // Iterate through save directory
    for (const auto& entry : std::filesystem::directory_iterator(_saveDirectory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".save") {
            try {
                std::ifstream file(entry.path());
                if (!file.is_open()) continue;
                
                nlohmann::json json;
                file >> json;
                file.close();
                
                SaveMetadata metadata;
                if (json.contains("metadata")) {
                    metadata.fromJson(json["metadata"]);
                    saves.push_back(metadata);
                }
            } catch (const std::exception&) {
                // Skip invalid save files
                continue;
            }
        }
    }
    
    return saves;
}

bool SaveManager::deleteSave(const std::string& slotName) {
    std::filesystem::path savePath = getSavePath(slotName);
    if (std::filesystem::exists(savePath)) {
        return std::filesystem::remove(savePath);
    }
    return false;
}

bool SaveManager::renameSave(const std::string& oldName, const std::string& newName) {
    std::filesystem::path oldPath = getSavePath(oldName);
    std::filesystem::path newPath = getSavePath(newName);
    
    if (!std::filesystem::exists(oldPath)) {
        return false;
    }
    
    try {
        std::filesystem::rename(oldPath, newPath);
        
        // Update metadata in file
        std::ifstream file(newPath);
        nlohmann::json json;
        file >> json;
        file.close();
        
        json["metadata"]["saveName"] = newName;
        
        std::ofstream outFile(newPath);
        outFile << json.dump(2);
        outFile.close();
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool SaveManager::copySave(const std::string& source, const std::string& dest) {
    std::filesystem::path sourcePath = getSavePath(source);
    std::filesystem::path destPath = getSavePath(dest);
    
    if (!std::filesystem::exists(sourcePath)) {
        return false;
    }
    
    try {
        std::filesystem::copy_file(sourcePath, destPath);
        
        // Update metadata in copied file
        std::ifstream file(destPath);
        nlohmann::json json;
        file >> json;
        file.close();
        
        json["metadata"]["saveName"] = dest;
        
        std::ofstream outFile(destPath);
        outFile << json.dump(2);
        outFile.close();
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void SaveManager::setPersistent(const std::string& key, const nlohmann::json& value) {
    _persistentState[key] = value;
}

nlohmann::json SaveManager::getPersistent(const std::string& key) const {
    if (_persistentState.contains(key)) {
        return _persistentState[key];
    }
    return nlohmann::json{};
}

bool SaveManager::hasPersistent(const std::string& key) const {
    return _persistentState.contains(key);
}

void SaveManager::clearPersistent(const std::string& key) {
    _persistentState.erase(key);
}

void SaveManager::clearAllPersistent() {
    _persistentState.clear();
}

void SaveManager::setSaveDirectory(const std::filesystem::path& dir) {
    _saveDirectory = dir;
    if (!std::filesystem::exists(_saveDirectory)) {
        std::filesystem::create_directories(_saveDirectory);
    }
}

void SaveManager::setAutoSaveInterval(float seconds) {
    _autoSaveInterval = seconds > 0.0f ? seconds : 300.0f;
}

void SaveManager::enableAutoSave(bool enable) {
    _autoSaveEnabled = enable;
    if (enable) {
        _autoSaveTimer = 0.0f;  // Reset timer
    }
}

void SaveManager::update(float dt) {
    if (!_autoSaveEnabled) {
        return;
    }
    
    _autoSaveTimer += dt;
    if (_autoSaveTimer >= _autoSaveInterval) {
        autoSave();
        _autoSaveTimer = 0.0f;
    }
}

std::filesystem::path SaveManager::getSavePath(const std::string& slotName) const {
    // Sanitize slot name (remove invalid characters)
    std::string sanitized = slotName;
    std::replace(sanitized.begin(), sanitized.end(), ' ', '_');
    std::replace(sanitized.begin(), sanitized.end(), '/', '_');
    std::replace(sanitized.begin(), sanitized.end(), '\\', '_');
    
    return _saveDirectory / (sanitized + ".save");
}

std::string SaveManager::generateTimestamp() const {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}
