#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "AssetConverter.hpp"
#include "AssetHandle.hpp"
#include "FileSystem.hpp"
#include "RawAsset.hpp"

// AssetManager: the core asset system. It's deliberately dumb about file
// formats — it loads raw bytes, issues AssetHandles, and dispatches to
// converters by file extension. The converters (registered by feature
// modules) are where format-specific decoding happens.
//
// Core ↔ module contract
// ----------------------
// - AssetManager stores RawAsset (raw bytes + path) keyed by AssetHandle.
//   It never touches the decoded form.
// - Each module registers a ConverterCallback for the extensions it handles
//   and owns its own typed decoded storage (use AssetRegistry<T>).
// - The converter callback is the bridge. It receives (RawAsset, AssetHandle)
//   and is expected to decode + stash the result in its module's registry
//   keyed by that same AssetHandle.
// - AssetHandle is the lingua franca: one identity, N views across
//   subsystems. `assets.loadAsset("foo.png")` returns a handle; the
//   texture module's `textures.get(handle)` returns the decoded texture.
// - loadAsset dedupes by path: a repeat load of the same path returns the
//   cached handle and does NOT re-run converters. This makes the handle a
//   stable key that scene deserializers and scripts can resolve on demand.
// - Converters run in registration order. Multiple converters for the same
//   extension are all invoked (observer-style). A converter that throws is
//   isolated: the load still succeeds and other converters still run.
// - Paths are manifest-root-relative, forward-slash canonicalized internally,
//   and always relative: absolute paths are rejected at the boundary. This
//   invariant is shared by `.jm.json`, scenes, and nested references inside
//   content files (e.g. the `binary` field in a `.script.json`). Enforcing it
//   means the engine behaves identically whether loading from loose files on
//   disk or a packed archive.
//
// Known limitation: no reverse AssetHandle -> path lookup.
// Paths are kept inside RawAsset but not exposed via the handle. Components
// that wish to serialize their asset reference (SpriteComponent's texture,
// AudioEmitterComponent's sound, ScriptComponent's script) currently can't
// round-trip. Marked in code as @TODO(asset-path-roundtrip). Fix when scene
// save-from-memory becomes a real need (editor, autosave): add
// `getPathByHandle(AssetHandle)` here, and have components store the source
// AssetHandle instead of just the decoded handle.
class AssetManager {
 public:
  AssetManager(const std::filesystem::path& root = ".");

  // Returns the handle for this path. If the path has already been loaded,
  // returns the cached handle without re-reading the file or re-running
  // converters.
  AssetHandle loadAsset(const std::filesystem::path& filePath);

  const RawAsset& getRawAsset(const AssetHandle& handle) const;

  // Register a converter for one or more file extensions (e.g. {".png",
  // ".jpg"}). Extensions are case-normalized internally. Multiple converters
  // may be registered for the same extension; they fire in registration
  // order.
  void addAssetConverter(const std::vector<std::string>& extensions, ConverterCallback callback);

 private:
  std::unordered_map<AssetHandle, RawAsset> _assets;
  std::unordered_map<std::string, AssetHandle> _pathToHandle;
  std::unordered_map<std::string, std::vector<ConverterCallback>> _converters;
  FileSystem _fileSystem;
  uint32_t _nextAssetId = 1;

  RawAsset loadRawBytes(const std::filesystem::path& filePath);
  void runConverters(const RawAsset& asset, const AssetHandle& handle);
};
