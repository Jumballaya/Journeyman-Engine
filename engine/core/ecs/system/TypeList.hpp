#pragma once

#include <type_traits>

template <typename... Types>
struct TypeList {};

using EmptyList = TypeList<>;

// contains<int, TypeList<float, int>>::value == true

// Match not found
template <typename T, typename List>
struct contains : std::false_type {};

// Match found
template <typename T, typename... Tail>
struct contains<T, TypeList<T, Tail...>> : std::true_type {};

// Recursive call
template <typename T, typename Head, typename... Tail>
struct contains<T, TypeList<Head, Tail...>> : contains<T, TypeList<Tail...>> {};

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

// Empty list (empty or at the end of the iteration)
template <>
struct TypeListForEach<TypeList<>> {
  template <typename Func>
  static void apply(Func&&) {}
};
