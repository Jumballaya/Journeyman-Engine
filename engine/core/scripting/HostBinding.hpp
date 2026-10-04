#pragma once

#include <wasm3.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>

#include "../ecs/entity/EntityId.hpp"
#include "../logger/logging.hpp"
#include "ScriptContext.hpp"

// Turns ordinary C++ callables into wasm3 host functions. The wasm signature
// is derived from the callable's parameter and return types:
//
//   C++ parameter        wasm params   notes
//   bool/int32/uint32    i
//   float / double       f / F
//   int64_t              I
//   std::string          i i           (ptr, len) UTF-8 bytes
//   AsString             i             an AssemblyScript string object (abort)
//   EntityId             i i           (index, generation)
//   WasmBytes            i i           (ptr, len) writable script memory
//   ScriptCall&          —             injected: the calling script
//
//   C++ return           wasm result
//   void / bool / ints / floats as above
//   EntityId             I   generation << 32 | index; -1 = no entity
//   std::optional<std::string>   i, plus trailing (outPtr, capacity) params:
//                        copies up to capacity bytes, returns the full byte
//                        length (callers retry with a bigger buffer) or -1.
//
// Out-of-range script pointers trap the calling script (it gets disabled with
// a log line); C++ exceptions are logged and trapped the same way, so they
// never unwind through wasm frames.
namespace host {

// The script instance that is calling into the host.
struct ScriptCall {
  IM3Runtime runtime;
  ScriptInstanceContext& script;

  EntityId self() const { return script.eid; }
  const nlohmann::json& params() const { return script.params; }
};

// A writable view of script memory.
struct WasmBytes {
  uint8_t* data = nullptr;
  size_t size = 0;
};

// The UTF-8 text of an AssemblyScript string object (UTF-16 in memory).
struct AsString {
  std::string text;
};

constexpr int64_t kNoEntity = -1;

namespace detail {

template <typename T>
struct Arg;  // per-type signature + decoding

class Reader {
 public:
  Reader(IM3Runtime runtime, uint64_t* sp) : _runtime(runtime), _sp(sp) {}

  template <typename T>
  T raw() {
    T value;
    std::memcpy(&value, _sp++, sizeof(T));
    return value;
  }

  uint8_t* memory(int32_t ptr, int64_t len) {
    uint32_t size = 0;
    uint8_t* mem = m3_GetMemory(_runtime, &size, 0);
    if (!mem || ptr < 0 || len < 0 || static_cast<uint64_t>(ptr) + static_cast<uint64_t>(len) > size) {
      failed = true;
      return nullptr;
    }
    return mem + ptr;
  }

  ScriptInstanceContext* context() { return static_cast<ScriptInstanceContext*>(m3_GetUserData(_runtime)); }
  IM3Runtime runtime() const { return _runtime; }

  bool failed = false;

 private:
  IM3Runtime _runtime;
  uint64_t* _sp;
};

template <typename T>
  requires(std::is_arithmetic_v<T>)
struct Arg<T> {
  static constexpr const char* sig() {
    if constexpr (std::is_same_v<T, float>) return "f";
    else if constexpr (std::is_same_v<T, double>) return "F";
    else if constexpr (sizeof(T) == 8) return "I";
    else return "i";
  }
  static T read(Reader& r) {
    if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double> || sizeof(T) == 8) return r.raw<T>();
    else return static_cast<T>(r.raw<int32_t>());
  }
};

template <>
struct Arg<std::string> {
  static constexpr const char* sig() { return "ii"; }
  static std::string read(Reader& r) {
    const int32_t ptr = r.raw<int32_t>();
    const int32_t len = r.raw<int32_t>();
    const uint8_t* p = r.memory(ptr, len);
    return p ? std::string(reinterpret_cast<const char*>(p), static_cast<size_t>(len)) : std::string();
  }
};

template <>
struct Arg<AsString> {
  static constexpr const char* sig() { return "i"; }
  static AsString read(Reader& r) {
    const int32_t ptr = r.raw<int32_t>();
    if (ptr == 0) return {};
    const uint8_t* header = r.memory(ptr - 4, 4);  // AS object header: byte length
    if (!header) return {};
    uint32_t bytes;
    std::memcpy(&bytes, header, 4);
    const uint8_t* p = r.memory(ptr, bytes);
    if (!p) return {};
    AsString out;
    for (uint32_t i = 0; i + 1 < bytes; i += 2) {
      const uint16_t unit = static_cast<uint16_t>(p[i] | (p[i + 1] << 8));
      out.text += unit < 0x80 ? static_cast<char>(unit) : '?';  // messages are ASCII in practice
    }
    return out;
  }
};

template <>
struct Arg<EntityId> {
  static constexpr const char* sig() { return "ii"; }
  static EntityId read(Reader& r) {
    const auto index = static_cast<uint32_t>(r.raw<int32_t>());
    const auto generation = static_cast<uint32_t>(r.raw<int32_t>());
    return EntityId{index, generation};
  }
};

template <>
struct Arg<WasmBytes> {
  static constexpr const char* sig() { return "ii"; }
  static WasmBytes read(Reader& r) {
    const int32_t ptr = r.raw<int32_t>();
    const int32_t len = r.raw<int32_t>();
    uint8_t* p = r.memory(ptr, len);
    return p ? WasmBytes{p, static_cast<size_t>(len)} : WasmBytes{};
  }
};

template <>
struct Arg<ScriptCall> {
  static constexpr const char* sig() { return ""; }
  static ScriptCall read(Reader& r) { return ScriptCall{r.runtime(), *r.context()}; }
};

template <typename R>
struct Result {
  static constexpr const char* sig() { return Arg<R>::sig(); }
  static constexpr const char* extraParams() { return ""; }
  static void write(uint64_t* slot, Reader&, const R& value) {
    if constexpr (std::is_same_v<R, float> || std::is_same_v<R, double> || sizeof(R) == 8) {
      std::memcpy(slot, &value, sizeof(R));
    } else {
      const int32_t v = static_cast<int32_t>(value);
      std::memcpy(slot, &v, sizeof(v));
    }
  }
};

template <>
struct Result<void> {
  static constexpr const char* sig() { return "v"; }
  static constexpr const char* extraParams() { return ""; }
};

template <>
struct Result<EntityId> {
  static constexpr const char* sig() { return "I"; }
  static constexpr const char* extraParams() { return ""; }
  static void write(uint64_t* slot, Reader&, const EntityId& id) {
    const int64_t packed = id.index == UINT32_MAX
                               ? kNoEntity
                               : static_cast<int64_t>((static_cast<uint64_t>(id.generation) << 32) | id.index);
    std::memcpy(slot, &packed, sizeof(packed));
  }
};

template <>
struct Result<std::optional<std::string>> {
  static constexpr const char* sig() { return "i"; }
  static constexpr const char* extraParams() { return "ii"; }
  static void write(uint64_t* slot, Reader& r, const std::optional<std::string>& value) {
    const int32_t outPtr = r.raw<int32_t>();
    const int32_t capacity = r.raw<int32_t>();
    int32_t result = -1;
    if (value) {
      uint8_t* out = r.memory(outPtr, capacity);
      if (out) std::memcpy(out, value->data(), std::min(value->size(), static_cast<size_t>(capacity)));
      result = static_cast<int32_t>(value->size());
    }
    std::memcpy(slot, &result, sizeof(result));
  }
};

template <typename F>
struct Callable : Callable<decltype(&F::operator())> {};
template <typename C, typename R, typename... A>
struct Callable<R (C::*)(A...) const> {
  using Return = R;
  using Args = std::tuple<std::decay_t<A>...>;
};
template <typename C, typename R, typename... A>
struct Callable<R (C::*)(A...)> {
  using Return = R;
  using Args = std::tuple<std::decay_t<A>...>;
};

template <typename Tuple>
struct ArgsSignature;
template <typename... A>
struct ArgsSignature<std::tuple<A...>> {
  static std::string get() { return (std::string() + ... + Arg<A>::sig()); }
};

}  // namespace detail

// A bound host function: its wasm signature and the thunk wasm3 calls.
class Binding {
 public:
  virtual ~Binding() = default;
  const std::string& signature() const { return _signature; }
  virtual M3RawCall thunk() const = 0;

 protected:
  std::string _signature;
};

template <typename F>
class BoundFunction final : public Binding {
 public:
  using Traits = detail::Callable<F>;
  using R = typename Traits::Return;
  using Args = typename Traits::Args;

  explicit BoundFunction(F fn) : _fn(std::move(fn)) {
    _signature = std::string(detail::Result<R>::sig()) + "(" + detail::ArgsSignature<Args>::get() +
                 detail::Result<R>::extraParams() + ")";
  }

  M3RawCall thunk() const override { return &call; }

 private:
  F _fn;

  static const void* call(IM3Runtime runtime, IM3ImportContext ctx, uint64_t* sp, void*) {
    auto* self = static_cast<BoundFunction*>(ctx->userdata);
    uint64_t* resultSlot = std::is_void_v<R> ? nullptr : sp++;
    detail::Reader reader(runtime, sp);
    try {
      // Braced init evaluates left to right: arguments decode in order.
      Args args = readArgs(reader, std::make_index_sequence<std::tuple_size_v<Args>>{});
      if (reader.failed) return m3Err_trapOutOfBoundsMemoryAccess;
      if constexpr (std::is_void_v<R>) {
        std::apply(self->_fn, args);
      } else {
        R value = std::apply(self->_fn, args);
        detail::Result<R>::write(resultSlot, reader, value);
        if (reader.failed) return m3Err_trapOutOfBoundsMemoryAccess;
      }
    } catch (const std::exception& e) {
      JM_LOG_ERROR("[Script] host function threw: {}", e.what());
      return m3Err_trapAbort;
    }
    return m3Err_none;
  }

  template <size_t... I>
  static Args readArgs(detail::Reader& reader, std::index_sequence<I...>) {
    return Args{detail::Arg<std::tuple_element_t<I, Args>>::read(reader)...};
  }
};

}  // namespace host
