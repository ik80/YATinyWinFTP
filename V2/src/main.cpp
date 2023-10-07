//
// refactored_echo_server.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2023 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <asio/write.hpp>
#include <cstdio>
#include <thread>
#include <memory>

using asio::ip::tcp;
using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;
namespace this_coro = asio::this_coro;

awaitable<void> echo_once(tcp::socket& socket, std::array<char,128> & data)
{
  std::size_t n = co_await socket.async_read_some(asio::buffer(data), use_awaitable);
  co_await async_write(socket, asio::buffer(data, n), use_awaitable);
}

awaitable<void> echo(tcp::socket socket)
{
  try
  {
    std::array<char,128> data;
    for (;;)
    {
      // The asynchronous operations to echo a single chunk of data have been
      // refactored into a separate function. When this function is called, the
      // operations are still performed in the context of the current
      // coroutine, and the behaviour is functionally equivalent.
      co_await echo_once(socket, data);
    }
  }
  catch (std::exception& e)
  {
    std::printf("echo Exception: %s\n", e.what());
  }
}

awaitable<void> listener(short unsigned int port)
{
  auto executor = co_await this_coro::executor;
  tcp::acceptor acceptor(executor, {tcp::v4(), port});
  for (;;)
  {
    tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
    co_spawn(executor, echo(std::move(socket)), detached);
  }
}

int main(int argc, char * argv[])
{
  try
  {
    int num_threads = argc > 1 ? atoi(argv[1]) : std::thread::hardware_concurrency();
    short unsigned int port = argc > 2 ? atoi(argv[2]) : 8888;

    asio::io_context io_context(num_threads);
    std::vector<std::shared_ptr<std::thread>> workers;
    workers.resize(num_threads);

    asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto){ io_context.stop(); });

    co_spawn(io_context, listener(port), detached);

    for (size_t i = 0; i < num_threads; ++i) 
      workers[i].reset(new std::thread([&](){io_context.run();}));

    for (size_t i = 0; i < num_threads; ++i) 
      workers[i]->join();

  }
  catch (std::exception& e)
  {
    std::printf("Exception: %s\n", e.what());
  }
}
