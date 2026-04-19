#pragma once

#include <wasm3.h>

#include "../core/app/Engine.hpp"
#include "AudioModule.hpp"

void setAudioHostContext(Engine&, AudioModule&);
void clearAudioHostContext();

m3ApiRawFunction(playSound);
m3ApiRawFunction(stopSound);
m3ApiRawFunction(fadeOutSound);
m3ApiRawFunction(setGainSound);