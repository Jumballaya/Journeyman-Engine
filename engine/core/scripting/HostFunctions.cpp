#include "HostFunctions.hpp"

#include "../app/Application.hpp"
#include "HostFunction.hpp"

static Application* currentApp = nullptr;

void setHostContext(Application& app) {
  currentApp = &app;
}

void clearHostContext() {
  currentApp = nullptr;
}

m3ApiRawFunction(log) {
  (void)_ctx;
  (void)_mem;
  m3ApiGetArg(int32_t, ptr);
  m3ApiGetArg(int32_t, len);

  uint8_t* memory = m3_GetMemory(runtime, nullptr, 0);
  std::string message(reinterpret_cast<char*>(memory + ptr), len);
  std::cout << "[script] " << message << "\n";

  m3ApiSuccess();
}

m3ApiRawFunction(abort) {
  (void)_ctx;
  (void)_mem;

  m3ApiGetArg(int32_t, msg_ptr);
  m3ApiGetArg(int32_t, file_ptr);
  m3ApiGetArg(int32_t, line);
  m3ApiGetArg(int32_t, column);

  uint8_t* memory = m3_GetMemory(runtime, nullptr, 0);

  std::string message(reinterpret_cast<char*>(memory + msg_ptr));
  std::string file(reinterpret_cast<char*>(memory + file_ptr));

  std::cerr << "[wasm abort] " << message << " at " << file << ":" << line << ":" << column << std::endl;

  m3ApiSuccess();
}
