#ifndef IRCOM_INCLUDE_IRCOM_DISCOVERY_H_
#define IRCOM_INCLUDE_IRCOM_DISCOVERY_H_

#include <condition_variable>
#include <mutex>

#include "avahi-client/client.h"
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

}  // namespace ircom::discovery

#endif
