#pragma once

class Engine;
class InputsModule;
class ScriptManager;

void setInputsHostContext(Engine&, InputsModule&);
void clearInputsHostContext();
void registerInputsHostFunctions(ScriptManager& scripts);
