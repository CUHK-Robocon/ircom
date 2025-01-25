#include "ircom/ircom.h"

#include <chrono>

#include "boost/system/system_error.hpp"
#include "spdlog/spdlog.h"

namespace ircom {

server::server(const char* service_name) : publisher_(service_name) {
  handle_thread_ = std::thread(&server::handle_thread_f, this);
}

void server::handle_thread_f() {
  publisher_.publish();

  while (true) {
    boost::asio::ip::tcp::socket socket(io_ctx_);
    acceptor_.accept(socket);
  }
}

void client::connect() {
  while (true) {
    const discovery::service_info& info = browser_.get_latest_service();

    try {
      boost::asio::ip::tcp::resolver::results_type endpoints =
          resolver_.resolve(info.addr, SERVICE_PORT_STR);

      boost::asio::ip::tcp::socket socket(io_ctx_);
      boost::asio::connect(socket, endpoints);

      break;
    } catch (boost::system::system_error err) {
      spdlog::warn("Failed to connect to remote, will retry: {}", err.what());
      // Retry cooldown.
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }
  }
}

}  // namespace ircom
