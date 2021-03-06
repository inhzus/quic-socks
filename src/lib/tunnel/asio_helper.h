//
// Created by suun on 2022/4/6.
// Copyright (c) 2022 Alibaba. All rights reserved.
//

#ifndef QUIC_SOCKS_TUNNEL_ASIO_CORE_H_
#define QUIC_SOCKS_TUNNEL_ASIO_CORE_H_

#include <asio.hpp>
#include <type_traits>

namespace socks::tunnel {

namespace detail {

template <typename, template <typename...> class>
struct IsSpec : std::false_type {};
template <template <typename...> class Ref, typename... Args>
struct IsSpec<Ref<Args...>, Ref> : std::true_type {};

template <typename Arg, typename... Args>
concept AwaitAbles = IsSpec<Arg, asio::awaitable>::value &&
    std::conjunction_v<std::is_same<Arg, Args>...>;

template <typename Ctx>
concept IsAsioContext = std::is_convertible_v<Ctx &, asio::execution_context &>;

template <size_t N, typename... Args>
using ArgIdx = std::tuple_element_t<N, std::tuple<Args...>>;

template <AwaitAbles... Args>
using AwaitAblesValueType = typename ArgIdx<0, Args...>::value_type;

template <typename T>
struct VectorCompVoid {
  using type = std::vector<T>;
};

template <>
struct VectorCompVoid<void> {
  using type = std::vector<int>;
};

}  // namespace detail

template <detail::IsAsioContext Ctx, detail::AwaitAbles... Args>
asio::awaitable<std::conditional_t<
    std::is_same_v<void, detail::AwaitAblesValueType<Args...>>, void,
    std::vector<detail::AwaitAblesValueType<Args...>>>>
WaitAll(Ctx &ctx, std::chrono::seconds timeout, Args &&...args) {
  asio::steady_timer barrier{ctx, timeout};
  std::atomic_size_t count{0};
  size_t idx{0};

  // return values only used when value type not void
  using ValueType = detail::AwaitAblesValueType<Args...>;
  constexpr bool ret_void = std::is_same_v<void, ValueType>;
  std::vector<std::conditional_t<ret_void, int, ValueType>> ret;
  std::mutex mutex;

  (asio::co_spawn(
       ctx,
       [c = std::move(args), &barrier, &count, cur = idx++, &mutex,
        &ret]() mutable -> asio::awaitable<void> {
         if constexpr (ret_void) {
           co_await std::move(c);
         } else {
           auto &&val = co_await std::move(c);
           std::lock_guard<std::mutex> lock{mutex};
           ret.emplace_back(std::forward<ValueType>(val));
         }
         if (++count == sizeof...(Args)) barrier.cancel();
       },
       asio::detached),
   ...);
  co_await barrier.async_wait(asio::use_awaitable);
  if constexpr (!ret_void) {
    co_return ret;
  }
}

}  // namespace socks::tunnel

#endif  // QUIC_SOCKS_TUNNEL_ASIO_CORE_H_
