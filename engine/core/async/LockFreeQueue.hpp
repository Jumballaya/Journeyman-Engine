#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <thread>

// Vyukov's bounded MPMC queue. References: https://www.1024cores.net/home
template <typename T>
class LockFreeQueue {
 public:
  // At capacity 1 the sequence numbers can't tell empty from full, so it is at least 2.
  explicit LockFreeQueue(size_t capacity)
      : _capacity(std::max<size_t>(capacity, 2)), _buffer(std::make_unique<Slot[]>(_capacity)) {
    for (size_t i = 0; i < _capacity; ++i) _buffer[i].sequence.store(i, std::memory_order_relaxed);
  }

  // Every position from head to tail holds a live item once no thread is using the queue.
  ~LockFreeQueue() {
    for (size_t pos = _head.load(); pos != _tail.load(); ++pos) _buffer[pos % _capacity].data()->~T();
  }

  LockFreeQueue(const LockFreeQueue&) = delete;
  LockFreeQueue& operator=(const LockFreeQueue&) = delete;

  // Afterwards every enqueue and dequeue fails.
  void shutdown() { _valid.store(false, std::memory_order_release); }

  bool try_enqueue(T&& item) {
    size_t pos;
    Slot* slot = claim(_tail, 0, pos);
    if (!slot) return false;  // full
    new (slot->data()) T(std::move(item));
    slot->sequence.store(pos + 1, std::memory_order_release);
    return true;
  }

  bool try_dequeue(T& out) {
    size_t pos;
    Slot* slot = claim(_head, 1, pos);
    if (!slot) return false;  // empty
    out = std::move(*slot->data());
    slot->data()->~T();
    slot->sequence.store(pos + _capacity, std::memory_order_release);
    return true;
  }

  size_t size_approx() const noexcept {
    return _tail.load(std::memory_order_relaxed) - _head.load(std::memory_order_relaxed);
  }

 private:
  struct Slot {
    std::atomic<size_t> sequence;
    alignas(alignof(T)) unsigned char storage[sizeof(T)];

    T* data() noexcept { return std::launder(reinterpret_cast<T*>(&storage)); }
  };

  // Advances `cursor` (head or tail) past a slot whose sequence is cursor + lag,
  // i.e. one ready for this side. Null when there is none (full or empty).
  Slot* claim(std::atomic<size_t>& cursor, size_t lag, size_t& pos) {
    if (!_valid.load(std::memory_order_acquire)) return nullptr;
    while (true) {
      pos = cursor.load(std::memory_order_relaxed);
      Slot& slot = _buffer[pos % _capacity];
      const auto diff = static_cast<intptr_t>(slot.sequence.load(std::memory_order_acquire)) -
                        static_cast<intptr_t>(pos + lag);
      if (diff < 0) return nullptr;
      if (diff == 0 && cursor.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) return &slot;
      if (diff > 0) std::this_thread::yield();  // another thread took it first
    }
  }

  size_t _capacity;
  std::unique_ptr<Slot[]> _buffer;
  alignas(64) std::atomic<size_t> _head{0};
  alignas(64) std::atomic<size_t> _tail{0};
  std::atomic<bool> _valid{true};
};
