#ifndef IRCOM_INCLUDE_IRCOM_IRCOM_H_
#define IRCOM_INCLUDE_IRCOM_IRCOM_H_

#include <thread>

#include "config.h"
#include "discovery.h"

// Before Boost.Asio 1.79.0, "boost/asio/awaitable.hpp" does not include
// <utility> causing `std::exchange` to be missing. Fixed by commit
// 71964b22c7fade69cc4caa1c869a868e3a32cc97. Backported to here.
// clang-format off
#include <utility>
#include "boost/asio.hpp"
// clang-format on

namespace ircom {

class server {
 public:
  explicit server(const char* service_name);
  ~server();

  server(const server&) = delete;
  server& operator=(const server&) = delete;

 private:
  boost::asio::awaitable<void> handle_conn();
  boost::asio::awaitable<void> handler();
  void io_ctx_thread_f();

  // IMPORTANT: The IO context MUST BE ran from one thread only, required for
  // graceful shutdown to work. Use a strand if multiple threads are needed.
  boost::asio::io_context io_ctx_;
  boost::asio::ip::tcp::acceptor acceptor_{
      io_ctx_,
      boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), SERVICE_PORT)};
  boost::asio::ip::tcp::socket sock_{io_ctx_};
  std::thread io_ctx_thread_;

  discovery::publisher publisher_;

  bool shutdown_issued_ = false;
};

class client {
 public:
  explicit client(const char* target_service_name);
  ~client();

  client(const client&) = delete;
  client& operator=(const client&) = delete;

 private:
  boost::asio::awaitable<void> handle_conn();
  boost::asio::awaitable<void> connect();
  void io_ctx_thread_f();

  discovery::browser browser_;

  // IMPORTANT: The IO context MUST BE ran from one thread only, required for
  // graceful shutdown to work. Use a strand if multiple threads are needed.
  boost::asio::io_context io_ctx_;
  boost::asio::ip::tcp::resolver resolver_{io_ctx_};
  boost::asio::ip::tcp::socket sock_{io_ctx_};
  std::thread io_ctx_thread_;

  bool shutdown_issued_ = false;
};

}  // namespace ircom

#endif
