#include "Renderer2DHostFunctions.hpp"

#include <string>

#include "../core/app/Engine.hpp"
#include "../core/ecs/World.hpp"
#include "../core/ecs/entity/EntityId.hpp"
#include "../core/logger/logging.hpp"
#include "../core/scripting/HostFunction.hpp"
#include "../core/scripting/ScriptManager.hpp"
#include "../core/scripting/WasmMemory.hpp"
#include "Renderer2DModule.hpp"
#include "SpriteAnimationComponent.hpp"
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

m3ApiRawFunction(jmSpriteSetAnimation) {
  m3ApiGetArg(int32_t, entityIndex);
  m3ApiGetArg(int32_t, entityGeneration);
  m3ApiGetArg(int32_t, namePtr);
  m3ApiGetArg(int32_t, nameLen);

  if (!currentEngine) {
    m3ApiSuccess();
  }

  uint32_t memSize = 0;
  uint8_t* memory = m3_GetMemory(runtime, &memSize, 0);
  if (!memory || namePtr < 0 || nameLen < 0 ||
      static_cast<uint32_t>(namePtr) + static_cast<uint32_t>(nameLen) >
          memSize) {
    m3ApiSuccess();
  }

  std::string animName(reinterpret_cast<char*>(memory + namePtr),
                       static_cast<size_t>(nameLen));

  // EntityId is {index, generation}. Both halves cross the wasm boundary so
  // this works for any recycled entity (generation > 0). Mirrors the
  // convention in ScriptInstance::onCollide which also passes both halves.
  EntityId eid{static_cast<uint32_t>(entityIndex),
               static_cast<uint32_t>(entityGeneration)};

  auto* comp =
      currentEngine->getWorld().getComponent<SpriteAnimationComponent>(eid);
  if (!comp) {
    JM_LOG_WARN(
        "[__jmSpriteSetAnimation] entity ({}, gen {}) has no "
        "SpriteAnimationComponent",
        entityIndex, entityGeneration);
    m3ApiSuccess();
  }

  if (!comp->animations.count(animName)) {
    JM_LOG_WARN(
        "[__jmSpriteSetAnimation] animation '{}' not found on entity ({}, "
        "gen {})",
        animName, entityIndex, entityGeneration);
    m3ApiSuccess();
  }

  comp->current = animName;
  comp->elapsed = 0.0f;
  comp->frameIndex = 0;
  comp->_finished = false;
  m3ApiSuccess();
}

m3ApiRawFunction(jmSpriteIsAnimationFinished) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, entityIndex);
  m3ApiGetArg(int32_t, entityGeneration);

  if (!currentEngine) {
    m3ApiReturn(0);
  }

  EntityId eid{static_cast<uint32_t>(entityIndex),
               static_cast<uint32_t>(entityGeneration)};
  auto* comp =
      currentEngine->getWorld().getComponent<SpriteAnimationComponent>(eid);
  if (!comp) {
    m3ApiReturn(0);
  }
  m3ApiReturn(comp->_finished ? 1 : 0);
}

// ---- Custom effects, camera, clear color -------------------------------------

m3ApiRawFunction(jmRendererAddCustom) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, pathPtr);
  m3ApiGetArg(int32_t, pathLen);
  auto path = wasm_memory::readString(runtime, pathPtr, pathLen);
  if (!currentRenderer2DModule || !path) m3ApiReturn(0);
  m3ApiReturn(static_cast<int32_t>(currentRenderer2DModule->addCustom(*path).id));
}

m3ApiRawFunction(jmRendererSetEffectUniformVec4) {
  m3ApiGetArg(int32_t, handleId);
  m3ApiGetArg(int32_t, namePtr);
  m3ApiGetArg(int32_t, nameLen);
  m3ApiGetArg(float, x);
  m3ApiGetArg(float, y);
  m3ApiGetArg(float, z);
  m3ApiGetArg(float, w);
  auto name = wasm_memory::readString(runtime, namePtr, nameLen);
  if (currentRenderer2DModule && name) {
    currentRenderer2DModule->setEffectUniform(PostEffectHandle{static_cast<uint32_t>(handleId)}, *name,
                                              glm::vec4(x, y, z, w));
  }
  m3ApiSuccess();
}

m3ApiRawFunction(jmCameraShake) {
  m3ApiGetArg(float, amplitude);
  m3ApiGetArg(float, duration);
  if (currentRenderer2DModule) currentRenderer2DModule->shake(amplitude, duration);
  m3ApiSuccess();
}

m3ApiRawFunction(jmCameraSetPosition) {
  m3ApiGetArg(float, x);
  m3ApiGetArg(float, y);
  if (currentRenderer2DModule) currentRenderer2DModule->setCameraPosition(glm::vec2(x, y));
  m3ApiSuccess();
}

m3ApiRawFunction(jmRendererSetClearColor) {
  m3ApiGetArg(float, r);
  m3ApiGetArg(float, g);
  m3ApiGetArg(float, b);
  m3ApiGetArg(float, a);
  if (currentRenderer2DModule) currentRenderer2DModule->setClearColor(glm::vec4(r, g, b, a));
  m3ApiSuccess();
}

void registerRenderer2DExtraHostFunctions(ScriptManager& scripts) {
  const HostFunction functions[] = {
      {"env", "__jmRendererAddCustom", "i(ii)", &jmRendererAddCustom},
      {"env", "__jmRendererSetEffectUniformVec4", "v(iiiffff)", &jmRendererSetEffectUniformVec4},
      {"env", "__jmCameraShake", "v(ff)", &jmCameraShake},
      {"env", "__jmCameraSetPosition", "v(ff)", &jmCameraSetPosition},
      {"env", "__jmRendererSetClearColor", "v(ffff)", &jmRendererSetClearColor},
  };
  for (const auto& fn : functions) scripts.registerHostFunction(fn.name, fn);
}
