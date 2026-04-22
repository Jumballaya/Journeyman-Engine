#pragma once

#include <wasm3.h>

#include "../core/app/Engine.hpp"
#include "Renderer2DModule.hpp"

void setRenderer2DHostContext(Engine&, Renderer2DModule&);
void clearRenderer2DHostContext();

m3ApiRawFunction(jmRendererAddBuiltin);
m3ApiRawFunction(jmRendererRemoveEffect);
m3ApiRawFunction(jmRendererSetEffectEnabled);
m3ApiRawFunction(jmRendererSetEffectUniformFloat);
m3ApiRawFunction(jmRendererSetEffectUniformVec3);
m3ApiRawFunction(jmRendererEffectCount);
