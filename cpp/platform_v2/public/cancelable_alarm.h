#ifndef PLATFORM_V2_PUBLIC_CANCELABLE_ALARM_H_
#define PLATFORM_V2_PUBLIC_CANCELABLE_ALARM_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "platform_v2/public/cancelable.h"
#include "platform_v2/public/mutex.h"
#include "platform_v2/public/mutex_lock.h"
#include "platform_v2/public/scheduled_executor.h"

namespace location {
namespace nearby {

/**
 * A cancelable alarm with a name. This is a simple wrapper around the logic
 * for posting a Runnable on a ScheduledExecutor and (possibly) later
 * canceling it.
 */
class CancelableAlarm {
 public:
  CancelableAlarm(absl::string_view name, std::function<void()>&& runnable,
                  absl::Duration delay, ScheduledExecutor* scheduled_executor)
      : name_(name),
        cancelable_(scheduled_executor->Schedule(std::move(runnable), delay)) {}
  ~CancelableAlarm() = default;
  CancelableAlarm(CancelableAlarm&& other) {
    *this = std::move(other);
  }
  CancelableAlarm& operator=(CancelableAlarm&& other) {
    MutexLock lock(&mutex_);
    {
      MutexLock other_lock(&other.mutex_);
      name_ = std::move(other.name_);
      cancelable_ = std::move(other.cancelable_);
    }
    return *this;
  }

  bool Cancel() {
    MutexLock lock(&mutex_);
    return cancelable_.Cancel();
  }

 private:
  Mutex mutex_;
  std::string name_;
  Cancelable cancelable_;
};

}  // namespace nearby
}  // namespace location

#endif  // PLATFORM_V2_PUBLIC_CANCELABLE_ALARM_H_