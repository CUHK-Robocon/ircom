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

  server(const server&) = delete;
  server& operator=(const server&) = delete;

 private:
  void handle_thread_f();

  boost::asio::io_context io_ctx_;
  boost::asio::ip::tcp::acceptor acceptor_{
      io_ctx_,
      boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), SERVICE_PORT)};

  std::thread handle_thread_;

  discovery::publisher publisher_;
};

class client {
 public:
  client() = default;

  client(const client&) = delete;
  client& operator=(const client&) = delete;

  void connect();

 private:
  discovery::browser browser_;

  boost::asio::io_context io_ctx_;
  boost::asio::ip::tcp::resolver resolver_{io_ctx_};
};

}  // namespace ircom

#endif
