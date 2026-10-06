#pragma once

#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

// A move-only void() callable stored inline (no allocation), up to MaxSize bytes.
template <size_t MaxSize = 128>
struct Job {
  Job() = default;
  ~Job() { reset(); }

  Job(const Job&) = delete;
  Job& operator=(const Job&) = delete;

  Job(Job&& other) noexcept { takeFrom(other); }
  Job& operator=(Job&& other) noexcept {
    if (this != &other) {
      reset();
      takeFrom(other);
    }
    return *this;
  }

  template <typename Fn>
  void set(Fn&& fn) {
    using M = Model<std::decay_t<Fn>>;
    static_assert(sizeof(M) <= MaxSize, "Job is too large for storage");
    reset();
    _base = new (_storage) M(std::forward<Fn>(fn));
  }

  void operator()() {
    assert(_base && "Job not set");
    _base->execute();
  }

  void reset() {
    if (!_base) return;
    _base->~Base();
    _base = nullptr;
  }

  bool valid() const { return _base != nullptr; }

 private:
  struct Base {
    virtual ~Base() = default;
    virtual void execute() = 0;
    virtual Base* moveTo(void* dest) = 0;
  };

  template <typename Fn>
  struct Model final : Base {
    Fn fn;

    explicit Model(Fn f) : fn(std::move(f)) {}
    void execute() override { fn(); }
    Base* moveTo(void* dest) override { return new (dest) Model(std::move(fn)); }
  };

  // Moves other's callable here and destroys its moved-from husk.
  void takeFrom(Job& other) {
    if (!other._base) return;
    _base = other._base->moveTo(_storage);
    other.reset();
  }

  alignas(std::max_align_t) unsigned char _storage[MaxSize];
  Base* _base = nullptr;
};
