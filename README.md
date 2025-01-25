# Inter-robot Communication

## Build Dependencies

- CMake
- C++ 20 compiler
- `avahi-client` and `avahi-common` libraries (`libavahi-client-dev` package for `apt`)
- Boost (`libboost-all-dev` package for `apt`)
- spdlog (`libspdlog-dev` package for `apt`)

## Runtime Requirements

- A running Avahi daemon (`avahi-daemon` package for `apt`)

## Known Issues

- Service updates (name, TXT records, etc changes) are not handled.
