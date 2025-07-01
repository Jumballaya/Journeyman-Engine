#pragma once

#include <wasm3.h>

#include "../core/app/Application.hpp"
#include "AudioModule.hpp"

void setAudioHostContext(Application&, AudioModule&);
void clearAudioHostContext();

m3ApiRawFunction(playSound);