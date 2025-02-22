#ifndef IRCOM_INCLUDE_IRCOM_DISCOVERY_H_
#define IRCOM_INCLUDE_IRCOM_DISCOVERY_H_

#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <string>
#include <vector>

#include "avahi-client/client.h"
#include "avahi-client/lookup.h"
#include "avahi-client/publish.h"
#include "avahi-common/thread-watch.h"

namespace ircom::discovery {

namespace internal {

class avahi_mutex {
 public:
  avahi_mutex() {}
  explicit avahi_mutex(AvahiThreadedPoll* ev_loop) : ev_loop_(ev_loop) {}

  void lock() { avahi_threaded_poll_lock(ev_loop_); }
  void unlock() { avahi_threaded_poll_unlock(ev_loop_); }

 private:
  AvahiThreadedPoll* ev_loop_ = nullptr;
};

enum publisher_state {
  PUBLISHER_STARTING,
  PUBLISHER_CAN_PUBLISH,
  PUBLISHER_PUBLISH_PENDING,
  PUBLISHER_PUBLISHED,
};

}  // namespace internal

class publisher {
 public:
  explicit publisher(const char* service_name);
  ~publisher();

  publisher(const publisher&) = delete;
  publisher& operator=(const publisher&) = delete;

  void publish();
  void reset();

 private:
  static void entry_group_callback(AvahiEntryGroup* entry_group,
                                   AvahiEntryGroupState state, void* data);
  static void client_callback(AvahiClient* client, AvahiClientState state,
                              void* data);

  void publish_service_unlocked();
  void set_state_unlocked(internal::publisher_state state);

  const char* service_name_;

  AvahiThreadedPoll* ev_loop_;
  AvahiClient* client_;
  AvahiEntryGroup* entry_group_ = nullptr;

  internal::publisher_state state_ =
      internal::publisher_state::PUBLISHER_STARTING;
  std::condition_variable_any state_update_cv_;

  internal::avahi_mutex mutex_;
};

struct service_info {
  AvahiIfIndex interface;
  std::string domain;

  std::string addr;
};

class closed_exception : public std::exception {
 public:
  const char* what() const noexcept override { return "Object closed"; }
};

class browser {
 public:
  explicit browser(const char* target_service_name);
  ~browser();

  browser(const browser&) = delete;
  browser& operator=(const browser&) = delete;

  service_info get_latest_service();

  void close();

 private:
  static void service_resolver_callback(
      AvahiServiceResolver* resolver, AvahiIfIndex interface,
      AvahiProtocol protocol, AvahiResolverEvent event, const char* name,
      const char* type, const char* domain, const char* host_name,
      const AvahiAddress* addr, uint16_t port, AvahiStringList* txt,
      AvahiLookupResultFlags flags, void* data);
  static void service_browser_callback(
      AvahiServiceBrowser* b, AvahiIfIndex interface, AvahiProtocol protocol,
      AvahiBrowserEvent event, const char* name, const char* type,
      const char* domain, AvahiLookupResultFlags flags, void* data);
  static void client_callback(AvahiClient* client, AvahiClientState state,
                              void* data);

  bool has_service_unlocked();

  const char* target_service_name_;

  AvahiThreadedPoll* ev_loop_;
  AvahiClient* client_;
  AvahiServiceBrowser* browser_;

  std::vector<service_info> services_;
  std::condition_variable_any new_service_cv_;

  bool is_closed_ = false;

  internal::avahi_mutex mutex_;
};

}  // namespace ircom::discovery

#endif
