#pragma once

class ScriptManager;
class UIModule;

void setUIHostContext(UIModule* module);
void registerUIHostFunctions(ScriptManager& scripts);
