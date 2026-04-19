#pragma once

#include <functional>

#include "AssetHandle.hpp"
#include "RawAsset.hpp"

// ConverterCallback runs when AssetManager finishes loading a raw asset whose
// file extension matches one the converter was registered for. The converter
// is expected to decode the raw bytes into whatever typed form its module
// uses, and stash it in a per-module AssetRegistry<T> keyed by the same
// AssetHandle. See AssetManager.hpp for the full contract.
using ConverterCallback = std::function<void(const RawAsset&, const AssetHandle&)>;
