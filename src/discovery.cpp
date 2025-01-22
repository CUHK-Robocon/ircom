#include "ircom/discovery.h"

#include <stdexcept>
#include <string>

#include "avahi-common/error.h"
#include "boost/format.hpp"
#include "ircom/config.h"

namespace ircom::discovery {

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
    return state_ == internal::publisher_state::CAN_PUBLISH ||
           state_ == internal::publisher_state::PUBLISH_PENDING ||
           state_ == internal::publisher_state::PUBLISHED;
  });
  switch (state_) {
    case internal::publisher_state::CAN_PUBLISH:
      publish_service_unlocked();
      set_state_unlocked(internal::publisher_state::PUBLISH_PENDING);
      break;
    case internal::publisher_state::PUBLISH_PENDING:
      // Another call to publish is publishing already.
      // Do nothing and directly wait for state to become PUBLISHED.
      break;
    case internal::publisher_state::PUBLISHED:
      return;
    default:
      throw std::logic_error("Unreachable branch");
  }
  // TODO: What if an error occurred or entry group is resetted while waiting?
  state_update_cv_.wait(
      lock, [&]() { return state_ == internal::publisher_state::PUBLISHED; });
}

void publisher::reset() {
  std::lock_guard<internal::avahi_mutex> lock(mutex_);
  if (entry_group_) {
    avahi_entry_group_reset(entry_group_);
    set_state_unlocked(internal::publisher_state::CAN_PUBLISH);
  }
}

void publisher::entry_group_callback(AvahiEntryGroup* entry_group,
                                     AvahiEntryGroupState state, void* data) {
  publisher* srv = static_cast<publisher*>(data);

  switch (state) {
    case AVAHI_ENTRY_GROUP_ESTABLISHED:
      srv->set_state_unlocked(internal::publisher_state::PUBLISHED);
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
    srv->set_state_unlocked(internal::publisher_state::STARTING);

  switch (state) {
    case AVAHI_CLIENT_S_RUNNING:
      srv->set_state_unlocked(internal::publisher_state::CAN_PUBLISH);
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

}  // namespace ircom::discovery
