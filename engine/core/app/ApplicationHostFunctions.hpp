#pragma once

#pragma once

#include <wasm3.h>

#include <iostream>
#include <string>

class Application;

m3ApiRawFunction(jmAbort);
m3ApiRawFunction(jmLog);
m3ApiRawFunction(jmEcsGetComponent);
m3ApiRawFunction(jmEcsUpdateComponent);

void clearHostContext();
void setHostContext(Application& app);
