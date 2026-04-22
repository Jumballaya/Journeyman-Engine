#include "Renderer2DHostFunctions.hpp"

#include <string>

#include "../core/app/Engine.hpp"
#include "../core/scripting/ScriptManager.hpp"
#include "Renderer2DModule.hpp"
#include "posteffects/PostEffectHandle.hpp"
#include "posteffects/builtins.hpp"

static Engine* currentEngine = nullptr;
static Renderer2DModule* currentRenderer2DModule = nullptr;

void setRenderer2DHostContext(Engine& app, Renderer2DModule& module) {
  currentEngine = &app;
  currentRenderer2DModule = &module;
}

void clearRenderer2DHostContext() {
  currentEngine = nullptr;
  currentRenderer2DModule = nullptr;
}

m3ApiRawFunction(jmRendererAddBuiltin) {
  (void)_ctx;
  (void)_mem;

  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, builtinId);

  if (!currentRenderer2DModule) {
    m3ApiReturn(0);
  }
  PostEffectHandle h = currentRenderer2DModule->addBuiltin(static_cast<BuiltinEffectId>(builtinId));
  m3ApiReturn(static_cast<int32_t>(h.id));
}

m3ApiRawFunction(jmRendererRemoveEffect) {
  (void)_ctx;
  (void)_mem;

  m3ApiGetArg(int32_t, handleId);

  if (!currentRenderer2DModule) {
    m3ApiSuccess();
  }
  currentRenderer2DModule->removeEffect(PostEffectHandle{static_cast<uint32_t>(handleId)});
  m3ApiSuccess();
}

m3ApiRawFunction(jmRendererSetEffectEnabled) {
  (void)_ctx;
  (void)_mem;

  m3ApiGetArg(int32_t, handleId);
  m3ApiGetArg(int32_t, enabled);

  if (!currentRenderer2DModule) {
    m3ApiSuccess();
  }
  currentRenderer2DModule->setEffectEnabled(PostEffectHandle{static_cast<uint32_t>(handleId)}, enabled != 0);
  m3ApiSuccess();
}

m3ApiRawFunction(jmRendererSetEffectUniformFloat) {
  m3ApiGetArg(int32_t, handleId);
  m3ApiGetArg(int32_t, namePtr);
  m3ApiGetArg(int32_t, nameLen);
  m3ApiGetArg(float, value);

  if (!currentRenderer2DModule) {
    m3ApiSuccess();
  }

  uint32_t memSize = 0;
  uint8_t* memory = m3_GetMemory(runtime, &memSize, 0);
  if (!memory || namePtr < 0 || nameLen < 0 ||
      static_cast<uint32_t>(namePtr) + static_cast<uint32_t>(nameLen) > memSize) {
    m3ApiSuccess();
  }

  std::string name(reinterpret_cast<char*>(memory + namePtr), static_cast<size_t>(nameLen));
  currentRenderer2DModule->setEffectUniform(
      PostEffectHandle{static_cast<uint32_t>(handleId)}, name, value);
  m3ApiSuccess();
}

m3ApiRawFunction(jmRendererSetEffectUniformVec3) {
  m3ApiGetArg(int32_t, handleId);
  m3ApiGetArg(int32_t, namePtr);
  m3ApiGetArg(int32_t, nameLen);
  m3ApiGetArg(float, x);
  m3ApiGetArg(float, y);
  m3ApiGetArg(float, z);

  if (!currentRenderer2DModule) {
    m3ApiSuccess();
  }

  uint32_t memSize = 0;
  uint8_t* memory = m3_GetMemory(runtime, &memSize, 0);
  if (!memory || namePtr < 0 || nameLen < 0 ||
      static_cast<uint32_t>(namePtr) + static_cast<uint32_t>(nameLen) > memSize) {
    m3ApiSuccess();
  }

  std::string name(reinterpret_cast<char*>(memory + namePtr), static_cast<size_t>(nameLen));
  currentRenderer2DModule->setEffectUniform(
      PostEffectHandle{static_cast<uint32_t>(handleId)}, name, glm::vec3(x, y, z));
  m3ApiSuccess();
}

m3ApiRawFunction(jmRendererEffectCount) {
  (void)_ctx;
  (void)_mem;

  m3ApiReturnType(int32_t);

  if (!currentRenderer2DModule) {
    m3ApiReturn(0);
  }
  m3ApiReturn(static_cast<int32_t>(currentRenderer2DModule->effectCount()));
}
