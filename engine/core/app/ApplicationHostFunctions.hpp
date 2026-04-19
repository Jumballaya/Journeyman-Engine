#pragma once

#pragma once

#include <wasm3.h>

#include <iostream>
#include <string>

class Engine;

m3ApiRawFunction(jmAbort);
m3ApiRawFunction(jmLog);
m3ApiRawFunction(jmEcsGetComponent);
m3ApiRawFunction(jmEcsUpdateComponent);

void clearHostContext();
void setHostContext(Engine& engine);
