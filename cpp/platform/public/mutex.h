#ifndef PLATFORM_PUBLIC_MUTEX_H_
#define PLATFORM_PUBLIC_MUTEX_H_

#include <memory>

#include "platform/api/mutex.h"
#include "platform/api/platform.h"
#include "absl/base/thread_annotations.h"

namespace location {
namespace nearby {

// This is a classic mutex can be acquired at most once.
// Atttempt to acuire mutex from the same thread that is holding it will likely
// cause a deadlock.
class ABSL_LOCKABLE Mutex final {
 public:
  using Platform = api::ImplementationPlatform;
  using Mode = api::Mutex::Mode;

  explicit Mutex(bool check = true)
      : impl_(Platform::CreateMutex(check ? Mode::kRegular
                                          : Mode::kRegularNoCheck)) {}
  Mutex(Mutex&&) = default;
  Mutex& operator=(Mutex&&) = default;
  ~Mutex() = default;

  void Lock() ABSL_EXCLUSIVE_LOCK_FUNCTION() { impl_->Lock(); }
  void Unlock() ABSL_UNLOCK_FUNCTION() { impl_->Unlock(); }

 private:
  friend class ConditionVariable;
  friend class MutexLock;
  std::unique_ptr<api::Mutex> impl_;
};

// This mutex is compatible with Java definition:
// https://docs.oracle.com/javase/8/docs/api/java/util/concurrent/locks/Lock.html
// This mutex may be acuired multiple times by a thread that is already holding
// it without blocking.
// It needs to be released equal number of times before any other thread could
// successfully acquire it.
class ABSL_LOCKABLE RecursiveMutex final {
 public:
  using Platform = api::ImplementationPlatform;
  using Mode = api::Mutex::Mode;

  RecursiveMutex() : impl_(Platform::CreateMutex(Mode::kRecursive)) {}
  RecursiveMutex(RecursiveMutex&&) = default;
  RecursiveMutex& operator=(RecursiveMutex&&) = default;
  ~RecursiveMutex() = default;

  void Lock() ABSL_EXCLUSIVE_LOCK_FUNCTION() { impl_->Lock(); }
  void Unlock() ABSL_UNLOCK_FUNCTION() { impl_->Unlock(); }

 private:
  friend class MutexLock;
  std::unique_ptr<api::Mutex> impl_;
};

}  // namespace nearby
}  // namespace location

#endif  // PLATFORM_PUBLIC_MUTEX_H_