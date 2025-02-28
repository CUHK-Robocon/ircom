#include "ircom/ircom.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

#include "boost/system/system_error.hpp"
#include "spdlog/spdlog.h"

namespace ircom {

void update_keeper::send_update(const packet::payload& pl) {
  boost::asio::co_spawn(sock_.get_executor(), flush_queue(pl),
                        boost::asio::detached);
}

packet::payload update_keeper::latest_update() {
  std::lock_guard<std::mutex> lock(latest_update_mtx_);
  return latest_update_;
}

boost::asio::awaitable<void> update_keeper::flush_queue(packet::payload pl) {
  if (!sock_.is_open()) {
    // To prevent updates from the last connection to be send to the new
    // connection.
    update_buf_.clear();
    co_return;
  }

  bool is_first = update_buf_.empty();

  if (update_buf_.size() == update_buf_.capacity()) {
    // Such that the active write loop knows whether to pop front or not.
    update_buf_rotated_ = true;
    spdlog::warn(
        "Outbound update buffer rotating, too many updates being dispatched");
  }
  update_buf_.push_back(pl);

  if (!is_first) co_return;

  do {
    // Make a copy here to prevent dangling reference/torn read as circular
    // buffer overwrites the front.
    packet::payload front = update_buf_.front();

    std::vector<std::uint8_t> payload_bytes;
    front.serialize(payload_bytes);

    std::array<boost::asio::const_buffer, 3> bufs = {
        packet::HEADER_BUF,
        boost::asio::buffer(payload_bytes),
        packet::FOOTER_BUF,
    };

    co_await boost::asio::async_write(sock_, bufs, boost::asio::use_awaitable);

    // If rotated, don't pop front as the front is no longer the same.
    // Reset "rotated" flag afterwards.
    if (!update_buf_rotated_) update_buf_.pop_front();
    update_buf_rotated_ = false;
  } while (!update_buf_.empty());
}

boost::asio::awaitable<void> update_keeper::handle_updates() {
  while (true) {
    std::uint8_t buf[5 + 24 + 3];
    co_await boost::asio::async_read(sock_,
                                     boost::asio::buffer(buf, 5 + 24 + 3),
                                     boost::asio::use_awaitable);

    packet::payload pl;
    pl.deserialize(buf + 5);

    {
      std::lock_guard<std::mutex> lock(latest_update_mtx_);
      latest_update_ = pl;
    }
  }
}

server::server(const char* service_name) : publisher_(service_name) {
  io_ctx_thread_ = std::thread(&server::io_ctx_thread_f, this);
}

server::~server() {
  boost::asio::post(io_ctx_, [&]() {
    sock_.close();
    acceptor_.close();
    shutdown_issued_ = true;
  });
  io_ctx_thread_.join();
}

void server::send_update(const packet::payload& pl) {
  udkeeper_.send_update(pl);
}

packet::payload server::latest_update() { return udkeeper_.latest_update(); }

boost::asio::awaitable<void> server::handler() {
  // Indeed, connections can reach the backlogs after `acceptor_` is opened.
  // Placed here instead of in the constructor just to minimize the time between
  // publication and service being actually available.
  publisher_.publish();
  spdlog::info("Service published");

  try {
    // Check against shutdown before the first iteration (e.g. when the
    // destrctor is called before the start of the IO context thread).
    while (!shutdown_issued_) {
      try {
        co_await acceptor_.async_accept(sock_, boost::asio::use_awaitable);
      } catch (const boost::system::system_error& err) {
        if (err.code() == boost::asio::error::operation_aborted) {
          spdlog::info("Acceptor shut down");
          break;
        }
        throw;
      }

      boost::asio::ip::tcp::endpoint remote_endpoint = sock_.remote_endpoint();
      spdlog::info("New connection from {}:{}",
                   remote_endpoint.address().to_string(),
                   remote_endpoint.port());

      try {
        co_await udkeeper_.handle_updates();
      } catch (const boost::system::system_error& err) {
        if (err.code() == boost::asio::error::operation_aborted ||
            err.code() == boost::asio::error::eof && shutdown_issued_) {
          spdlog::info("Ongoing communication shut down");
          break;
        }

        spdlog::error(
            "An error occurred for the connection, discarding connection: {}",
            err.what());
        sock_.close();
        continue;
      }
    }
  } catch (const std::exception& err) {
    spdlog::critical("An error is uncaught in the server handler: {}",
                     err.what());
    io_ctx_.stop();
    co_return;
  }
}

void server::io_ctx_thread_f() {
  boost::asio::co_spawn(io_ctx_, handler(), boost::asio::detached);
  io_ctx_.run();
}

client::client(const char* target_service_name)
    : browser_(target_service_name) {
  io_ctx_thread_ = std::thread(&client::io_ctx_thread_f, this);
}

client::~client() {
  browser_.close();
  boost::asio::post(io_ctx_, [&]() {
    resolver_.cancel();
    sock_.close();
    shutdown_issued_ = true;
  });
  io_ctx_thread_.join();
}

void client::send_update(const packet::payload& pl) {
  udkeeper_.send_update(pl);
}

packet::payload client::latest_update() { return udkeeper_.latest_update(); }

boost::asio::awaitable<void> client::connect() {
  try {
    // Check against shutdown before the first iteration (e.g. when the
    // destrctor is called before the start of the IO context thread).
    while (!shutdown_issued_) {
      spdlog::info("Discovering services");
      discovery::service_info info;
      try {
        // TODO: Use an async version of `get_latest_service`.
        info = browser_.get_latest_service();
      } catch (const discovery::closed_exception&) {
        spdlog::info("Service discovery stopped");
        break;
      }
      spdlog::info("Selected service @ {}", info.addr);

      bool should_retry = false;
      try {
        boost::asio::ip::tcp::resolver::results_type endpoints =
            co_await resolver_.async_resolve(info.addr, SERVICE_PORT_STR,
                                             boost::asio::use_awaitable);

        spdlog::info("Connecting to service");
        co_await boost::asio::async_connect(sock_, endpoints,
                                            boost::asio::use_awaitable);
        spdlog::info("Connected to service");
      } catch (const boost::system::system_error& err) {
        if (err.code() == boost::asio::error::operation_aborted) {
          spdlog::info("Connection attempt cancelled");
          break;
        }

        spdlog::warn("Failed to connect to remote, will retry: {}", err.what());
        should_retry = true;
      }
      if (should_retry) {
        sock_.close();

        // Retry cooldown.
        boost::asio::steady_timer timer(io_ctx_,
                                        boost::asio::chrono::seconds(1));
        co_await timer.async_wait(boost::asio::use_awaitable);

        continue;
      }

      try {
        co_await udkeeper_.handle_updates();
      } catch (const boost::system::system_error& err) {
        if (err.code() == boost::asio::error::operation_aborted ||
            err.code() == boost::asio::error::eof && shutdown_issued_) {
          spdlog::info("Ongoing communication shut down");
          break;
        }

        spdlog::error(
            "An error occurred for the connection, discarding connection: {}",
            err.what());
        sock_.close();
        continue;
      }
    }
  } catch (const std::exception& err) {
    spdlog::critical("An error is uncaught in the client connection loop: {}",
                     err.what());
    io_ctx_.stop();
    co_return;
  }
}

void client::io_ctx_thread_f() {
  boost::asio::co_spawn(io_ctx_, connect(), boost::asio::detached);
  io_ctx_.run();
}

}  // namespace ircom
