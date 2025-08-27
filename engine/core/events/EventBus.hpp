#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "../async/LockFreeQueue.hpp"
#include "../logger/logging.hpp"
#include "Event.hpp"

class EventBus {
 public:
  using Token = uint64_t;

  explicit EventBus(size_t capacity = 8192)
      : _queue(capacity), _pendingUnsub(capacity) {}

  template <typename T>
  Token subscribe(std::function<void(const T&)> fn) {
    std::lock_guard lk(_subMutex);
    const auto key = std::type_index(typeid(T));
    const Token tok = ++_nextToken;
    _typed[key].push_back({tok, [f = std::move(fn)](const Event& e) {
                             f(std::get<T>(e));
                           }});
    return tok;
  }

  Token subscribeDynamic(uint32_t id, std::function<void(const events::DynamicEvent&)> fn) {
    std::lock_guard lk(_subMutex);
    const Token tok = ++_nextToken;
    _dyn[id].push_back({tok, [f = std::move(fn)](const events::DynamicEvent& ev) { f(ev); }});
    return tok;
  }

  void unsubscribe(Token tok);

  // @TODO: Throttle the emit based on an event-by-event throttle amount in ms
  template <typename T>
  void emit(const T& e) {
    _queue.try_enqueue(Event{e});
  }

  void emitDynamic(uint32_t id, std::string name, nlohmann::json data) {
    events::DynamicEvent ev{id, std::move(name), std::move(data)};
    _queue.try_enqueue(Event{std::move(ev)});
  }

  void dispatch(size_t maxEvents = SIZE_MAX);

  void shutdown() {
    _queue.shutdown();
    _pendingUnsub.shutdown();
  }

 private:
  struct TypedSub {
    Token tok;
    std::function<void(const Event&)> fn;
  };
  struct DynSub {
    Token tok;
    std::function<void(const events::DynamicEvent&)> fn;
  };

  template <typename T>
  void dispatchTyped(const Event& e) {
    std::lock_guard lk(_subMutex);
    auto it = _typed.find(std::type_index(typeid(T)));
    if (it == _typed.end()) return;
    auto subs = it->second;  // copy to allow unsubscribe during callbacks
    for (auto& s : subs) s.fn(e);
  }

  void dispatchDynamic(const events::DynamicEvent& e) {
    std::lock_guard lk(_subMutex);
    auto it = _dyn.find(e.id);
    if (it == _dyn.end()) return;
    auto subs = it->second;  // copy to allow unsubscribe during callbacks
    for (auto& s : subs) s.fn(e);
  }

  void eraseToken(Token tok);

  // queues
  LockFreeQueue<Event> _queue;
  LockFreeQueue<Token> _pendingUnsub;

  // subscribers
  std::mutex _subMutex;
  std::unordered_map<std::type_index, std::vector<TypedSub>> _typed;
  std::unordered_map<uint32_t, std::vector<DynSub>> _dyn;

  Token _nextToken = 0;
};
