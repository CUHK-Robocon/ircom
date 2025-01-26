#include "ircom/ircom.h"

#include <chrono>

#include "boost/system/system_error.hpp"
#include "spdlog/spdlog.h"

namespace ircom {

server::server(const char* service_name) : publisher_(service_name) {
  handle_thread_ = std::thread(&server::handle_thread_f, this);
}

void server_handle_conn(boost::asio::ip::tcp::socket& sock) {
  while (true) {
    // TODO: Exchange packet.

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

void server::handle_thread_f() {
  publisher_.publish();
  spdlog::info("Service published");

  while (true) {
    try {
      boost::asio::ip::tcp::socket sock(io_ctx_);
      acceptor_.accept(sock);

      boost::asio::ip::tcp::endpoint remote_endpoint = sock.remote_endpoint();
      spdlog::info("New connection from {}:{}",
                   remote_endpoint.address().to_string(),
                   remote_endpoint.port());

      server_handle_conn(sock);
    } catch (boost::system::system_error err) {
      spdlog::error(
          "An error occurred for the connection, discarding connection: {}",
          err.what());
      continue;
    }
  }
}

void client_handle_conn(boost::asio::ip::tcp::socket& sock) {
  while (true) {
    // TODO: Exchange packet.
  }
}

void client::connect() {
  while (true) {
    spdlog::info("Discovering services");
    const discovery::service_info& info = browser_.get_latest_service();
    spdlog::info("Selected service @ {}", info.addr);

    boost::asio::ip::tcp::socket sock(io_ctx_);
    try {
      boost::asio::ip::tcp::resolver::results_type endpoints =
          resolver_.resolve(info.addr, SERVICE_PORT_STR);

      spdlog::info("Connecting to service");
      boost::asio::connect(sock, endpoints);
      spdlog::info("Connected to service");
    } catch (boost::system::system_error err) {
      spdlog::warn("Failed to connect to remote, will retry: {}", err.what());
      // Retry cooldown.
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }

    try {
      client_handle_conn(sock);
    } catch (boost::system::system_error err) {
      spdlog::error(
          "An error occurred for the connection, discarding connection: {}",
          err.what());
      continue;
    }
  }
}

}  // namespace ircom
