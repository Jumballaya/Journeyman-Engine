#pragma once

#include <utility>

template <typename... Types>
struct TypeList {};

using EmptyList = TypeList<>;

// TypeListForEach<TypeList<A, B>>::apply(f) calls f.template operator()<A>(), then <B>.

template <typename List>
struct TypeListForEach;

template <typename Head, typename... Tail>
struct TypeListForEach<TypeList<Head, Tail...>> {
  template <typename Func>
  static void apply(Func&& func) {
    func.template operator()<Head>();
    TypeListForEach<TypeList<Tail...>>::apply(std::forward<Func>(func));
  }
};

template <>
struct TypeListForEach<TypeList<>> {
  template <typename Func>
  static void apply(Func&&) {}
};
