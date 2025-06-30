#include "HostFunctions.hpp"

#include "../core/app/Application.hpp"
#include "HostFunction.hpp"

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

static Application* currentApp = nullptr;

std::vector<HostFunction> coreHostFunctions = {
    {"env", "jmLog", "v(ii)", &log},
    {"env", "abort", "v(iiii)", &abort},
};

void linkCommonHostFunctions(IM3Module module, Application& app) {
  currentApp = &app;

  for (const auto& host : coreHostFunctions) {
    M3Result result = m3_LinkRawFunction(module, host.module, host.name, host.signature, host.function);
    if (result != m3Err_none) {
      throw std::runtime_error("Failed to link host function [" + std::string(host.name) + "]: " + std::string(result));
    }
  }
}

void clearHostContext() {
  currentApp = nullptr;
}