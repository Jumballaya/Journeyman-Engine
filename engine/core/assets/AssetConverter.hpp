#pragma once

#include <functional>

#include "AssetHandle.hpp"
#include "RawAsset.hpp"

// Decodes a just-loaded RawAsset into its module's AssetRegistry under the same
// handle.
using ConverterCallback = std::function<void(const RawAsset&, const AssetHandle&)>;
