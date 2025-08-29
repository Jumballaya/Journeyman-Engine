#pragma once

#include <wasm3.h>

#include "../core/app/Application.hpp"
#include "InputsModule.hpp"

void setInputsHostContext(Application&, InputsModule&);
void clearInputsHostContext();

m3ApiRawFunction(jmKeyIsPressed);