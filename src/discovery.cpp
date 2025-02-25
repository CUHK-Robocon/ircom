#include "ircom/discovery.h"

#include <cstring>
#include <stdexcept>
#include <string>

#include "avahi-common/address.h"
#include "avahi-common/error.h"
#include "boost/format.hpp"
#include "ircom/config.h"
#include "spdlog/spdlog.h"

namespace ircom::discovery {

// Should be the same as the protocol the server is listening over.
const int DISCOVERY_RESOLVE_ADDR_PROTO = AVAHI_PROTO_INET;
const char* const DISCOVERY_SERVICE_TYPE = "_ircom._tcp";

publisher::publisher(const char* service_name) : service_name_(service_name) {
  ev_loop_ = avahi_threaded_poll_new();
  if (!ev_loop_) throw std::runtime_error("Failed to create Avahi event loop");

  mutex_ = internal::avahi_mutex(ev_loop_);

  int error;
  client_ =
      avahi_client_new(avahi_threaded_poll_get(ev_loop_), AVAHI_CLIENT_NO_FAIL,
                       client_callback, this, &error);
  if (!client_) {
    avahi_threaded_poll_free(ev_loop_);

    throw std::runtime_error(
        (boost::format("Failed to create Avahi client: %1%") %
         avahi_strerror(error))
            .str());
  }

  avahi_threaded_poll_start(ev_loop_);
}

publisher::~publisher() {
  avahi_threaded_poll_stop(ev_loop_);

  if (entry_group_) avahi_entry_group_free(entry_group_);
  avahi_client_free(client_);
  avahi_threaded_poll_free(ev_loop_);
}

void publisher::publish() {
  std::unique_lock<internal::avahi_mutex> lock(mutex_);
  state_update_cv_.wait(lock, [&]() {
    return state_ == internal::publisher_state::PUBLISHER_CAN_PUBLISH ||
           state_ == internal::publisher_state::PUBLISHER_PUBLISH_PENDING ||
           state_ == internal::publisher_state::PUBLISHER_PUBLISHED;
  });
  switch (state_) {
    case internal::publisher_state::PUBLISHER_CAN_PUBLISH:
      publish_service_unlocked();
      set_state_unlocked(internal::publisher_state::PUBLISHER_PUBLISH_PENDING);
      break;
    case internal::publisher_state::PUBLISHER_PUBLISH_PENDING:
      // Another call to publish is publishing already.
      // Do nothing and directly wait for state to become PUBLISHER_PUBLISHED.
      break;
    case internal::publisher_state::PUBLISHER_PUBLISHED:
      return;
    default:
      throw std::logic_error("Unreachable branch");
  }
  // TODO: What if an error occurred or entry group is resetted while waiting?
  state_update_cv_.wait(lock, [&]() {
    return state_ == internal::publisher_state::PUBLISHER_PUBLISHED;
  });
}

void publisher::reset() {
  std::lock_guard<internal::avahi_mutex> lock(mutex_);
  if (entry_group_) {
    avahi_entry_group_reset(entry_group_);
    set_state_unlocked(internal::publisher_state::PUBLISHER_CAN_PUBLISH);
  }
}

void publisher::entry_group_callback(AvahiEntryGroup* entry_group,
                                     AvahiEntryGroupState state, void* data) {
  publisher* srv = static_cast<publisher*>(data);

  switch (state) {
    case AVAHI_ENTRY_GROUP_ESTABLISHED:
      srv->set_state_unlocked(internal::publisher_state::PUBLISHER_PUBLISHED);
      break;

    case AVAHI_ENTRY_GROUP_COLLISION:
      // Remote name collision handled here.
      // Local name collision is handled when the service is added to a entry
      // group.
      avahi_threaded_poll_quit(srv->ev_loop_);
      throw std::runtime_error("Remote service name collision");

    case AVAHI_ENTRY_GROUP_FAILURE: {
      avahi_threaded_poll_quit(srv->ev_loop_);

      std::string err_msg = avahi_strerror(
          avahi_client_errno(avahi_entry_group_get_client(entry_group)));
      throw std::runtime_error(
          (boost::format("Avahi entry group has an error: %1%") % err_msg)
              .str());
    }

    default:
      break;
  }
}

void publisher::client_callback(AvahiClient* client, AvahiClientState state,
                                void* data) {
  publisher* srv = static_cast<publisher*>(data);

  if (state != AVAHI_CLIENT_S_RUNNING)
    srv->set_state_unlocked(internal::publisher_state::PUBLISHER_STARTING);

  switch (state) {
    case AVAHI_CLIENT_S_RUNNING:
      srv->set_state_unlocked(internal::publisher_state::PUBLISHER_CAN_PUBLISH);
      break;

    case AVAHI_CLIENT_S_COLLISION:
    case AVAHI_CLIENT_S_REGISTERING:
      srv->reset();
      break;

    case AVAHI_CLIENT_FAILURE:
      avahi_threaded_poll_quit(srv->ev_loop_);

      throw std::runtime_error(
          (boost::format("Avahi client has an error: %1%") %
           avahi_strerror(avahi_client_errno(client)))
              .str());

    default:
      break;
  }
}

void publisher::publish_service_unlocked() {
  if (!entry_group_) {
    entry_group_ = avahi_entry_group_new(client_, entry_group_callback, this);
    if (!entry_group_)
      throw std::runtime_error("Failed to create Avahi entry group");
  }

  if (!avahi_entry_group_is_empty(entry_group_)) {
    throw std::logic_error(
        "Avahi entry group is not empty when trying to publish services");
  }

  // Publish over all protocols for maximum coverage. With the default daemon
  // configuration, both IPv4 and IPv6 records are available over IPv4 queries;
  // IPv6 records are available over IPv6 queries.
  int ret = avahi_entry_group_add_service(
      entry_group_, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
      static_cast<AvahiPublishFlags>(0), service_name_, DISCOVERY_SERVICE_TYPE,
      nullptr, nullptr, SERVICE_PORT, nullptr);
  if (ret == AVAHI_ERR_COLLISION) {
    // Local name collision handled here.
    // Remote name collision is handled in entry group callback.

    // NOTE: May use alternative name if needed in the future.

    throw std::runtime_error(
        "Local service name collision, maybe another process is still "
        "running");
  }
  if (ret < 0) {
    throw std::runtime_error(
        (boost::format("Failed to add service to Avahi entry group: %1%") %
         avahi_strerror(ret))
            .str());
  }

  ret = avahi_entry_group_commit(entry_group_);
  if (ret < 0) {
    throw std::runtime_error(
        (boost::format("Failed to commit Avahi entry group: %1%") %
         avahi_strerror(ret))
            .str());
  }
}

void publisher::set_state_unlocked(internal::publisher_state state) {
  state_ = state;
  state_update_cv_.notify_all();
}

browser::browser(const char* target_service_name)
    : target_service_name_(target_service_name) {
  ev_loop_ = avahi_threaded_poll_new();
  if (!ev_loop_) throw std::runtime_error("Failed to create Avahi event loop");

  mutex_ = internal::avahi_mutex(ev_loop_);

  int error;
  client_ =
      avahi_client_new(avahi_threaded_poll_get(ev_loop_), AVAHI_CLIENT_NO_FAIL,
                       client_callback, this, &error);
  if (!client_) {
    avahi_threaded_poll_free(ev_loop_);

    throw std::runtime_error(
        (boost::format("Failed to create Avahi client: %1%") %
         avahi_strerror(error))
            .str());
  }

  // Query over all protocols for maximum coverage. With the default daemon
  // configuration, both IPv4 and IPv6 records are available over IPv4 queries;
  // IPv6 records are available over IPv6 queries. Address type are later
  // filtered when creating resolver.
  browser_ = avahi_service_browser_new(
      client_, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, DISCOVERY_SERVICE_TYPE,
      nullptr, static_cast<AvahiLookupFlags>(0), service_browser_callback,
      this);
  if (!browser_) {
    avahi_client_free(client_);
    avahi_threaded_poll_free(ev_loop_);

    throw std::runtime_error(
        (boost::format("Failed to create Avahi service browser: %1%") %
         avahi_strerror(avahi_client_errno(client_)))
            .str());
  }

  avahi_threaded_poll_start(ev_loop_);
}

browser::~browser() {
  avahi_threaded_poll_stop(ev_loop_);

  avahi_service_browser_free(browser_);
  avahi_client_free(client_);
  avahi_threaded_poll_free(ev_loop_);
}

service_info browser::get_latest_service() {
  std::unique_lock<internal::avahi_mutex> lock(mutex_);
  new_service_cv_.wait(lock,
                       [&]() { return has_service_unlocked() || is_closed_; });
  if (is_closed_) throw closed_exception();
  return services_.back();
}

void browser::close() {
  {
    std::unique_lock<internal::avahi_mutex> lock(mutex_);
    is_closed_ = true;
  }
  // Notify all CVs.
  new_service_cv_.notify_all();

  // Should be placed after `is_closed_` is set to true, such that existing CVs
  // are all waken up to handle closure while new waits on the CVs will return
  // immediately.
  avahi_threaded_poll_stop(ev_loop_);
}

void browser::service_resolver_callback(
    AvahiServiceResolver* resolver, AvahiIfIndex interface,
    AvahiProtocol protocol, AvahiResolverEvent event, const char* name,
    const char* type, const char* domain, const char* host_name,
    const AvahiAddress* addr, uint16_t port, AvahiStringList* txt,
    AvahiLookupResultFlags flags, void* data) {
  browser* self = static_cast<browser*>(data);

  switch (event) {
    case AVAHI_RESOLVER_FOUND:
      if (protocol != DISCOVERY_RESOLVE_ADDR_PROTO) break;
      if (port != SERVICE_PORT) break;

      char addr_cstr[AVAHI_ADDRESS_STR_MAX];
      avahi_address_snprint(addr_cstr, AVAHI_ADDRESS_STR_MAX, addr);

      self->services_.push_back({
          .interface = interface,
          .domain = domain,
          .addr = addr_cstr,
      });

      self->new_service_cv_.notify_all();

      spdlog::debug(
          "Found new service (name: {}, interface: {}, domain: {}, address: "
          "{})",
          name, interface, domain, addr_cstr);

      break;

    case AVAHI_RESOLVER_FAILURE:
      // The failure may not be fatal, e.g. when querying INET record over
      // INET6.
      spdlog::debug(
          "Cannot resolve a `{}` service (`{}` in domain `{}`) over protocol "
          "{}, skipping: {}",
          type, name, domain, protocol,
          avahi_strerror(
              avahi_client_errno(avahi_service_resolver_get_client(resolver))));
      break;
  }

  avahi_service_resolver_free(resolver);
}

void browser::service_browser_callback(
    AvahiServiceBrowser* b, AvahiIfIndex interface, AvahiProtocol protocol,
    AvahiBrowserEvent event, const char* name, const char* type,
    const char* domain, AvahiLookupResultFlags flags, void* data) {
  browser* self = static_cast<browser*>(data);

  switch (event) {
    case AVAHI_BROWSER_NEW:
      if (std::strcmp(name, self->target_service_name_) != 0) break;
      if (std::strcmp(type, DISCOVERY_SERVICE_TYPE) != 0) break;

      // Address type filter specified here.
      // Resolver is freed in the callback.
      if (!avahi_service_resolver_new(avahi_service_browser_get_client(b),
                                      interface, protocol, name, type, domain,
                                      DISCOVERY_RESOLVE_ADDR_PROTO,
                                      static_cast<AvahiLookupFlags>(0),
                                      service_resolver_callback, self)) {
        throw std::runtime_error(
            (boost::format("Failed to create Avahi service resolver: %1%") %
             avahi_strerror(
                 avahi_client_errno(avahi_service_browser_get_client(b))))
                .str());
      }

      break;

    case AVAHI_BROWSER_REMOVE:
      if (std::strcmp(name, self->target_service_name_) != 0) break;
      if (std::strcmp(type, DISCOVERY_SERVICE_TYPE) != 0) break;
      if (protocol != DISCOVERY_RESOLVE_ADDR_PROTO) break;

      for (auto it = self->services_.begin(); it != self->services_.end();) {
        const service_info& service = *it;
        if (service.interface == interface && service.domain == domain) {
          it = self->services_.erase(it);
          spdlog::debug(
              "Removed service (name: {}, interface: {}, domain: {}, address: "
              "{})",
              name, interface, domain, service.addr);
        } else {
          ++it;
        }
      }

      break;

    case AVAHI_BROWSER_FAILURE:
      avahi_threaded_poll_quit(self->ev_loop_);

      throw std::runtime_error(
          (boost::format("Avahi service browser has an error: %1%") %
           avahi_strerror(
               avahi_client_errno(avahi_service_browser_get_client(b))))
              .str());

    default:
      break;
  }
}

void browser::client_callback(AvahiClient* client, AvahiClientState state,
                              void* data) {
  browser* bs = static_cast<browser*>(data);
  if (state == AVAHI_CLIENT_FAILURE) {
    avahi_threaded_poll_quit(bs->ev_loop_);
    throw std::runtime_error((boost::format("Avahi client has an error: %1%") %
                              avahi_strerror(avahi_client_errno(client)))
                                 .str());
  }
}

bool browser::has_service_unlocked() { return !services_.empty(); }

}  // namespace ircom::discovery
