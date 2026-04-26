#include "SceneHostFunctions.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

#include "../logger/logging.hpp"
#include "Engine.hpp"
#include "SceneManager.hpp"

// Host functions run on WORKER threads (scripts execute via the JobSystem).
// They MUST NOT mutate SceneManager state directly. All scene mutations are
// enqueued via SceneManager::requestLoad / requestTransition; SceneManager::tick
// drains the queue on the main thread.

static Engine* s_currentEngine = nullptr;
static SceneManager* s_currentSceneManager = nullptr;

void setSceneHostContext(Engine& engine, SceneManager& sceneManager) {
  s_currentEngine = &engine;
  s_currentSceneManager = &sceneManager;
}

void clearSceneHostContext() {
  s_currentEngine = nullptr;
  s_currentSceneManager = nullptr;
}

m3ApiRawFunction(jmSceneLoad) {
  (void)_ctx;
  (void)_mem;

  m3ApiGetArg(int32_t, namePtr);
  m3ApiGetArg(int32_t, nameLen);

  if (!s_currentSceneManager) {
    m3ApiSuccess();
  }

  uint32_t memSize = 0;
  uint8_t* memory = m3_GetMemory(runtime, &memSize, 0);
  if (!memory || namePtr < 0 || nameLen < 0 ||
      static_cast<uint32_t>(namePtr) + static_cast<uint32_t>(nameLen) > memSize) {
    JM_LOG_WARN("[SceneHost] jmSceneLoad: out-of-bounds pointer/length");
    m3ApiSuccess();
  }

  std::string path(reinterpret_cast<char*>(memory + namePtr),
                   static_cast<size_t>(nameLen));
  s_currentSceneManager->requestLoad(std::filesystem::path(path));
  m3ApiSuccess();
}

m3ApiRawFunction(jmSceneTransition) {
  (void)_ctx;
  (void)_mem;

  m3ApiGetArg(int32_t, namePtr);
  m3ApiGetArg(int32_t, nameLen);
  m3ApiGetArg(float, durationSeconds);

  if (!s_currentSceneManager) {
    m3ApiSuccess();
  }

  uint32_t memSize = 0;
  uint8_t* memory = m3_GetMemory(runtime, &memSize, 0);
  if (!memory || namePtr < 0 || nameLen < 0 ||
      static_cast<uint32_t>(namePtr) + static_cast<uint32_t>(nameLen) > memSize) {
    JM_LOG_WARN("[SceneHost] jmSceneTransition: out-of-bounds pointer/length");
    m3ApiSuccess();
  }

  std::string path(reinterpret_cast<char*>(memory + namePtr),
                   static_cast<size_t>(nameLen));
  TransitionConfig config{};
  config.duration = durationSeconds;
  s_currentSceneManager->requestTransition(std::filesystem::path(path), config);
  m3ApiSuccess();
}

m3ApiRawFunction(jmSceneIsTransitioning) {
  (void)_ctx;
  (void)_mem;

  m3ApiReturnType(int32_t);

  if (!s_currentSceneManager) {
    m3ApiReturn(0);
  }
  m3ApiReturn(s_currentSceneManager->isTransitioning() ? 1 : 0);
}
