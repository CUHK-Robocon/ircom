#include "ircom/packet.h"

#include <cstring>

#include "boost/endian.hpp"

namespace ircom::packet {

void payload::serialize(std::vector<std::uint8_t>& out) const {
  boost::endian::big_float64_buf_t f64_buf;

  f64_buf = x;
  out.insert(out.end(), f64_buf.data(), f64_buf.data() + sizeof(f64_buf));

  f64_buf = y;
  out.insert(out.end(), f64_buf.data(), f64_buf.data() + sizeof(f64_buf));

  f64_buf = t;
  out.insert(out.end(), f64_buf.data(), f64_buf.data() + sizeof(f64_buf));
}

void payload::deserialize(const std::uint8_t* in) {
  boost::endian::big_float64_buf_t f64_buf;
  int idx = 0;

  std::memcpy(f64_buf.data(), in + idx, sizeof(f64_buf));
  x = f64_buf.value();
  idx += sizeof(f64_buf);

  std::memcpy(f64_buf.data(), in + idx, sizeof(f64_buf));
  y = f64_buf.value();
  idx += sizeof(f64_buf);

  std::memcpy(f64_buf.data(), in + idx, sizeof(f64_buf));
  t = f64_buf.value();
  idx += sizeof(f64_buf);
}

}  // namespace ircom::packet
