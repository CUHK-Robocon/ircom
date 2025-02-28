#ifndef IRCOM_INCLUDE_IRCOM_PACKET_H_
#define IRCOM_INCLUDE_IRCOM_PACKET_H_

#include <cstddef>
#include <cstdint>
#include <vector>

// Before Boost.Asio 1.79.0, "boost/asio/awaitable.hpp" does not include
// <utility> causing `std::exchange` to be missing. Fixed by commit
// 71964b22c7fade69cc4caa1c869a868e3a32cc97. Backported to here.
// clang-format off
#include <utility>
#include "boost/asio.hpp"
// clang-format on

namespace ircom::packet {

const char HEADER[] = "ircom";
const std::size_t HEADER_SIZE = sizeof(HEADER) - 1;  // Ignore null termination.
const boost::asio::const_buffer HEADER_BUF =
    boost::asio::buffer(HEADER, HEADER_SIZE);

const char FOOTER[] = "end";
const std::size_t FOOTER_SIZE = sizeof(FOOTER) - 1;  // Ignore null termination.
const boost::asio::const_buffer FOOTER_BUF =
    boost::asio::buffer(FOOTER, FOOTER_SIZE);

struct payload {
  double x;
  double y;
  double t;

  void serialize(std::vector<std::uint8_t>& out) const;
  void deserialize(const std::uint8_t* in);
};

}  // namespace ircom::packet

#endif
