#ifndef IRCOM_INCLUDE_IRCOM_CONFIG_H_
#define IRCOM_INCLUDE_IRCOM_CONFIG_H_

#include <cstdint>
#include <string>

namespace ircom {

const std::uint16_t SERVICE_PORT = 40001;
const std::string SERVICE_PORT_STR = std::to_string(SERVICE_PORT);

}  // namespace ircom

#endif
